call set-windows-env.bat

pushd ..
qmake.exe librecad.pro -r -spec win64-g++
if not _%1==_NoClean (
	mingw64-make.exe clean
)
mingw64-make.exe -j4
if NOT exist windows\LibreCAD.exe (
	echo "Building windows\LibreCAD.exe failed!"
	exit /b /1
)
windeployqt.exe windows\LibreCAD.exe
popd
call build-win-setup.bat
