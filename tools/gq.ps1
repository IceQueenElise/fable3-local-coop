# Run Query.java against the saved Ghidra project.
# Usage: gq.ps1 <outName> <cmd> [<cmd> ...]   (output: docs\ghidra\<outName>.txt)
param([Parameter(Mandatory)] [string] $OutName, [Parameter(ValueFromRemainingArguments)] [string[]] $Commands)

$env:JAVA_HOME = 'C:\Program Files\Microsoft\jdk-21.0.12.101-hotspot'
$out = "F:\Fable3Coop\docs\ghidra\$OutName.txt"
& 'F:\Tools\ghidra_12.1.4_PUBLIC\support\analyzeHeadless.bat' 'F:\Fable3Coop\ghidra' 'Fable3' -process 'Fable3.exe' `
	-noanalysis -readOnly -scriptPath 'F:\Fable3Coop\tools\ghidra_scripts' `
	-postScript Query.java $out @Commands *> "F:\Fable3Coop\logs\gq_$OutName.log"
if (Test-Path $out) { Get-Content $out } else { Get-Content "F:\Fable3Coop\logs\gq_$OutName.log" -Tail 30 }
