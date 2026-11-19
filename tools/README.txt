Using compile.cmd (CMD or PowerShell)
cmd
:: Build Release (default)
.\tools\compile.cmd
:: Build Debug or RelWithDebInfo
.\tools\compile.cmd Debug
.\tools\compile.cmd RelWithDebInfo
:: Clean rebuild
.\tools\compile.cmd rebuild
:: Build a specific target
.\tools\compile.cmd -t xrEngine

Using compile.ps1 (PowerShell)

powershell
# Build Release (default)
.\tools\compile.ps1
# Build Debug
.\tools\compile.ps1 -Config Debug
# Clean rebuild
.\tools\compile.ps1 -Clean -Config Release
# Build specific target with custom core count
.\tools\compile.ps1 -Target xrGame -Parallel 8