:: For the communication between the wimm wrapper and the cdaudioplr.exe 
:: to work the latter needs to be started first. This is done
:: internally by the wrapper but due to possible delays caused by Windows
:: there might be cases where the cduadioplr program is not yet running
:: when the winmm wrapper starts to ask for information via the mailslot.
:: This in turn may lead to an unresponsive game and no music playback.
:: To avoid this situation an alternative way is to manually start the
:: cdaudioplr.exe before the game program. This batch file automates it.

cd mcicda
START cdaudioplr.exe

:: A timeout in seconds can be specified to wait for program to be ready.
timeout 3

cd..

:: Change game.exe to match the name of your game executable.
START "" /WAIT "game.exe"
:: Or to run in single core affinity use: START "" /WAIT /AFFINITY 2 "game.exe"

:: Kill cdaudioplr.exe in case auto close fails.
taskkill /im cdaudioplr.exe /t /f
