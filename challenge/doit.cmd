@rem echo off
@cls
@del challenge_*
@rmdir arena /s /q 
@mkdir arena
@pushd arena

@rem set OTP=\dev\bin\otp.exe
@rem set OTP=\dev\bin\otp-qt.exe
@set OTP=call %~dp0..\otp.cmd
@echo Using %OTP% for OTP commands

@rem generate initial keys
%OTP% -g -y keys/AA

@rem copy two keys to current directory
copy keys\AA-024.otk key1.otk
copy keys\AA-025.otk key2.otk

@rem create combined key
%OTP% -k -j -i key1.otk -a key2.otk -o combinedkey.otk -y keys/BB

copy combinedkey.otk ..

@rem uncombined key
%OTP% -k -u -i key1.otk -a key2.otk -c combinedkey.otk -y keys/CC

@rem validate to ensure they are the same:
diff keys\BB-001.otk keys\CC-001.otk

@rem encrypt message
%OTP% -k -e -z -i EDITOR -o secret_msg.otp keys\BB-001.otk

copy secret_msg.otp ..

@rem decrypt message
%OTP% -k -d -z -i secret_msg.otp -o EDITOR keys\CC-001.otk

tar -czf ../challenge_solution.tgz .

del keys\AA*.otk
del keys\BB*.otk
del keys\CC-001.otk
del key1.otk
del key2.otk

tar -czf ../challenge_artifacts.tgz .

cd ..
@certutil -hashfile challenge_solution.tgz SHA256 > challenge_solution.tgz.hash.txt
@certutil -hashfile challenge_artifacts.tgz SHA256 > challenge_artifacts.tgz.hash.txt

popd
rmdir /s /q arena
cls
dir
@echo Done.