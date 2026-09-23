@echo off
rem echo "Invoke win_parent_process2_mt_ppp.test"
rem echo "Parameters: #0 : <batch file>, #1 : <pin exe>, #2 <pin command line>, #3 <working directory>"
%~f1 %~2 -- cmd /C win_parent_process_mt_launcher.bat %~3