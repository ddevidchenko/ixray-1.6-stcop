
#include "stdafx.h"
#include "NET_Common.h"
#include "Multiplayer/LegacyMultipacket.h"

//------------------------------------------------------------------------------

static NET_Compressor Compressor;
static_assert(NET_PacketSizeLimit == Multiplayer::Legacy::RecordSizeLimit);
static_assert(NET_TAG_NONMERGED == Multiplayer::Legacy::SingleTag && NET_TAG_MERGED == Multiplayer::Legacy::MergedTag);
static_assert(NET_TAG_NONCOMPRESSED == Multiplayer::Legacy::RawTag && NET_TAG_COMPRESSED == Multiplayer::Legacy::CompressedTag);

XRNETSERVER_API int     psNET_GuaranteedPacketMode  = NET_GUARANTEEDPACKET_DEFAULT;

MultipacketSender::MultipacketSender()
{
}

void MultipacketSender::SendPacket( const void* packet_data, u32 packet_sz, u32 flags, u32 timeout )
{
    _buf_cs.Enter();
	
    Buffer* buf = &_buf;

    switch( psNET_GuaranteedPacketMode )
    {
        case NET_GUARANTEEDPACKET_IGNORE :
        {
            flags &= ~DPNSEND_GUARANTEED;
        }   break;

        case NET_GUARANTEEDPACKET_SEPARATE :
        {
            if( flags & DPNSEND_GUARANTEED )
                buf = &_gbuf;
        }   break;
    }
        
    u32 old_flags = (buf->last_flags) & (~DPNSEND_IMMEDIATELLY);
    u32 new_flags = flags & (~DPNSEND_IMMEDIATELLY);

    if(     (buf->buffer.B.count + packet_sz + sizeof(u16) >= NET_PacketSizeLimit)
        ||  (old_flags != new_flags)
        ||  (flags & DPNSEND_IMMEDIATELLY)
      )
    {
        _FlushSendBuffer( timeout, buf );
    }

    buf->buffer.w_u16( (u16)packet_sz );
    buf->buffer.w( packet_data, packet_sz );
    buf->last_flags = flags;
        
    if( flags & DPNSEND_IMMEDIATELLY )
        _FlushSendBuffer( timeout, buf );
    
    _buf_cs.Leave();
}

//------------------------------------------------------------------------------

void MultipacketSender::FlushSendBuffer( u32 timeout )
{
    _buf_cs.Enter();
    
    _FlushSendBuffer( timeout, &_buf );
    _FlushSendBuffer( timeout, &_gbuf );
    
    _buf_cs.Leave();
}

//------------------------------------------------------------------------------

void MultipacketSender::_FlushSendBuffer( u32 timeout, Buffer* buf )
{
    // expected to be called between '_buf_cs' enter/leave
    if( buf->buffer.B.count )
    {
        using namespace Multiplayer::Legacy;
        xr_array<u8, MaxDatagramSize> Packet;
        const auto Body = xr_span(Packet).subspan<HeaderSize>();
        const u16 CompressedSize = Compressor.Compress(Body.data(), static_cast<u32>(Body.size()),
            buf->buffer.B.data, buf->buffer.B.count);
        if (CompressedSize == 0)
        {
            buf->buffer.B.count = 0;
            return;
        }

        Packet.front() = MergedTag;
        Multiplayer::Wire::Detail::WriteLittleEndian<u16>(xr_span(Packet).subspan<1, sizeof(u16)>(),
            static_cast<u16>(buf->buffer.B.count));
		
        // dump/log if needed

        #if NET_LOG_PACKETS
        Msg( "#send %smulti-packet %u    flags= %08X", 
             (buf->last_flags & DPNSEND_IMMEDIATELLY)?"IMMEDIATE ":"",
             buf->buffer.B.count, buf->last_flags 
           );
        #endif // NET_LOG_PACKETS

	    if( strstr( Core.Params,"-dump_traffic") ) 
        {
            static bool first_time  = true;
			FILE*       dump        = fopen( "raw-out-traffic.bins", (first_time)?"wb":"ab" );

            if( first_time )
            {
                fwrite( "BINS", 4, 1, dump );
                first_time = false;
            }

            u16 sz = u16(buf->buffer.B.count);
            
            fwrite( &sz, sizeof(u16), 1, dump );
            fwrite( buf->buffer.B.data, buf->buffer.B.count, 1, dump );
			fclose( dump );
        }
		
        // do send
        
        _SendTo_LL(Packet.data(), static_cast<u32>(HeaderSize + CompressedSize), buf->last_flags, timeout);
        buf->buffer.B.count = 0;        
    } // if buffer not empty
}

//------------------------------------------------------------------------------

void MultipacketReciever::RecievePacket(const void* packet_data, u32 packet_sz, u32 param)
{
	using namespace Multiplayer::Legacy;
	if (!packet_data)
	{
		return;
	}

#if !NET_USE_COMPRESSION
	constexpr auto Framing = BodyFraming::Untagged;
#elif NET_USE_COMPRESSION_CRC
	constexpr auto Framing = BodyFraming::Checksummed;
#else
	constexpr auto Framing = BodyFraming::Tagged;
#endif
	const auto Datagram = xr_span(static_cast<const u8*>(packet_data), packet_sz);
	const auto Envelope = ReadEnvelope(Datagram, Framing);
	if (!Envelope)
	{
		return;
	}

	const auto Dispatch = [&](xr_span<const u8> Body)
	{
		auto Records = MultipacketRecords::Validate(*Envelope, Body);
		if (!Records)
		{
			return;
		}

#if NET_LOG_PACKETS
		Msg("#receive multi-packet %u", packet_sz);
#endif
		if (strstr(Core.Params, "-dump_traffic"))
		{
			static bool First = true;
			if (FILE* Dump = fopen("raw-in-traffic.bins", First ? "wb" : "ab"))
			{
				const xr_scope_exit Close([Dump] { fclose(Dump); });
				if (First)
				{
					fwrite("BINS", 4, 1, Dump);
					First = false;
				}
				const u16 Size = Envelope->DecodedSize;
				fwrite(&Size, sizeof(Size), 1, Dump);
				fwrite(Body.data(), Body.size(), 1, Dump);
			}
		}

		while (const auto Record = Records->Next())
		{
			const auto Size = static_cast<u32>(Record->size());
#if NET_LOG_PACKETS
			Msg("  packet %u", Size);
#endif
			_Recieve(Record->data(), Size, param);
		}
	};

	if (Envelope->Encoding == BodyEncoding::Raw)
	{
		Dispatch(Envelope->Payload);
		return;
	}

	xr_array<u8, MaxDecodedSize> Storage;
	const auto Packed = Datagram.subspan(HeaderSize);
	const u16 Produced = Compressor.Decompress(Storage.data(), Envelope->DecodedSize,
		const_cast<u8*>(Packed.data()), static_cast<u32>(Packed.size()));
	if (Produced == Envelope->DecodedSize)
	{
		Dispatch(xr_span(Storage).first(Produced));
	}
}

void XRNETSERVER_API DumpNetCompressorStats(bool brief)
{
	Compressor.DumpStats(brief);
}
