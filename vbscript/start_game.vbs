'This is a vbscript to launch cdaudioplr.exe followed by a wait and the game program launch.
Dim objShell
Set objShell = WScript.CreateObject( "WScript.Shell" )
objShell.Run(".\\mcicda\\cdaudioplr.exe")

'Sleep for 1000 milliseconds (1 second)
WScript.Sleep 1000

'Change the GAME.exe to match your game executable name:
objShell.Run("GAME.exe")
wscript.quit
