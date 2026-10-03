@echo off
cd /d %~dp0

echo ============================================
echo  Building v1.0
echo ============================================
cd v1.0
call build-MSM.bat
call build-SMP.bat
cd ..

echo.
echo ============================================
echo  Building v2.0
echo ============================================
cd v2.0
call build-MSM.bat
call build-SMP.bat
cd ..

echo.
echo All done.
pause