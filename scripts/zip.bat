@ECHO OFF
ECHO Zipping cellox and creating installer using NSIS ...
cd ../build/src
ECHO Zipping cellox ...
cpack -G ZIP --config ../CPackConfig.cmake
:: Create an installer using NSIS (nullsoft scriptable install system) (https://sourceforge.net/projects/nsis/) for Windows
ECHO Creating installer using NSIS ...
cpack -G NSIS64 --config ../CPackConfig.cmake