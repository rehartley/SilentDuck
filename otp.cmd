@echo off
rem Runs otp.py from source, forwarding all arguments untouched.
rem Lets you type "otp ..." instead of "python otp.py ..." once this
rem folder is on PATH, or by calling it with a full path.
python "%~dp0otp.py" %*
