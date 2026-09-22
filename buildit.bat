python -m PyInstaller --onefile --name otp otp_wrapper.py
copy dist\otp.exe \dev\bin\
copy otp.py \dev\bin
dir \dev\bin\otp.exe dist\otp.exe
