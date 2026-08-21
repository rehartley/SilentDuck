# SilentDuck


Title:
------

SILENT DUCK - Usable one time pad encryption


Introduction:
-------------

This app automates a manual one time pad (OTP) encryption procedure field tested as far back as the 1970s and likely earlier. It has been publicly disclosed since early 1977.

SILENT DUCK (SD) is the name of the app, and is not the term used by the creators of this procedure.  SD automates manual one time pad (OTP) encryption of text, digits and some punctuation characters.  

WARNING:
1) This app will automatically wipe and delete input files for each command.
2) Using the "-k" flag inhibits this auto-wipe and delete behaviour
3) There is also a test flag "-t" which prevents all file output

SILENT DUCK (SD) operates with the following Roman and Cyrillic letters, numberals, and punctuation marks:

A B C D E F G H I J K L M N O P Q R S T U V W X Y Z
А Б В Г Д Е Ж З И Й К Л М Н О П Р С Т У Ф Х Ц Ч Ш Щ Ы Ь Э Ю Я
0 1 2 3 4 5 6 7 8 9
 ? : @ / ~ # .
 
The space " ", and new line "\n", characters are included but hard to represent in print.

Missing from the Cyrillic alphabet are "Ё" ("yo") and "Ъ" ( "твёрдый знак", or "tyordy znak", the hard sign)

Encrypted messages use letters converted to digits '0' through '9'.  When transmitting messages via Morse code, these can be represented with letters referred to as "morse shorts":
"t a u v 4 e 6 b d n"

Further details at:
https://www.numbers-stations.com/articles/trigon-numbers-station-the-case-of-alexandr-ogorodnik/


Manual OTP Procedure:
---------------------

The one time pad system is extremely simple.

Encryption of messages is performed by:
0) generating and sharing a series of random digits with the sender and recipient
1) converting letters of a message to digits
2) subtracting a series of pre-shared key random digits from those digits 
3) sending the resultto the recipient
4) destroying the keys used.

To decrypt the message, the recipient will add the series of digits received to the random ones previously shared, and decode the results back into letters. At this point, the set of digits used is destroyed to avoid compromise.


Procedure:
----------

Before communication starts, a sets of random numbers is generated for use as keys.  A copy is shared with the sender and the recipient.  These numbers must be completely random, and for best security, the only copies in existance should be the two held by the sender and the receiver.

The series of numbers are frequently arranged in five digit groups, five to a line for ten lines to a page, and a single one time pad booklet might be twenty five pages long.  Size can vary among teams, but this seems to be a rather common format is is the standard used here.

A typical page looks like this:

32146 33752 72251 41211 26504 
92831 09110 42569 82819 34052 
40168 47763 91666 39615 46721 
14776 60041 42426 64131 01759 
81156 22641 72955 12896 00978 
94402 07280 13013 96272 05105 
46372 00120 73361 65891 05426 
47239 18329 80971 17676 85711 
04666 38355 84094 97535 78406 
66597 12934 77393 75786 79187 

As message are encrypted, the page with the keys used to encrypt them are destroyed to avoid discovery. The pads should be hidden securely while not in use, and ought to have some evidence of being tampered with such as perforation and wax/glue seals, colour shifting inks that activate when exposed to light, etc.

Letters are converted to digits using "straddling checkerboard" lookup tables.  These help reduce the size of the encrypted message that needs to be sent.  Some common seven letters are converted to the seven digits '0' through '6', and the remainder are converted to double digits in the range of 70 through 99 using the row in the table as the first digit, with the column number as the second digit.

The straddling checkboards for Latin and Cyrillic letters are shown here:

    Latin Alphabet:
    
            0   1   2   3   4   5   6   7   8   9
            ======================================
            A   E   N   R   O   I   T   _   _   _
         7  B   C   G   D   F   H   J   K   L   M
         8  P   Q   S   U   V   W   X   Y   Z   _
         9  ?   :   @   /   #   .   ,   \n ' '  ~


    Cyrillic Alphabet:
    
            0   1   2   3   4   5   6   7   8   9
            ======================================
            А   Е   И   Н   О   С   Т   _   _   _
         7  Б   В   Г   Д   Ж   З   Й   К   Л   М
         8  П   Р   У   Ф   Х   Ц   Ч   Ш   Щ   Ы
         9  Ь   Э   Ю   Я   #   .   ,   \n ' '  ~


Using the tables above, the English-Russian greeting "HELLO, ПРИВЕТ." is encoded as digits, using the tilde '~' as the alphabet swap code to switch between Latin and Cyrillic letters:

H   E   L   L   O   ,   ' '  ~   П   Р   И   В   Е   Т  .  ~
75  1   78  78  4   96  98   99  80  83  2   70  1   6  95 99


To encode digits, use the '#' to start and then double each digit, ending with a final '#'.  This way, the year '1985' encodes to '#11998855#'

To generate encrypted text, subtract (without carrying) the encoded string of letters from the sample OTP key's digits:

         H E  L  L O  , ' ' ~  П  Р И  В Е Т  .  ~
        75 1 78 78 4 96 98 99 80 83 2 70 1 6 95 99

The text encoded into numbers is then written out below the random key that was share previously with the recipient, and then subtracted:

Plain Text:   75 1 78 78 49 69 89 9 80 8 3 27 01 6 95 99 
Random key: - 32 1 46 33 75 27 22 5 14 1 2 11 26 5 04 92
              ------------------------------------------
Difference:   43 0 32 45 74 42 67 4 76 7 1 16 85 1 01 07 

Rearranging into five digit groups gives: 
43032 45744 26747 67116 85101 07

Use a standard "Morse Shorts" table, since Morse code's letters take less transmission time than numbers 
0 1 2 3 4 5 6 7 8 9
T A U V 4 E 6 B D N


The final message ready for transmission becomes:
4VTVU 4EB44 U6B4B 7BAA6 DE1T1 TB


The message is now ready to send in Morse code.

On the recipient's end, the process is:
    a) transcribe the Morse
    b) convert to digits
    c) write the key value down
    d) sum the key values to the enciphered digits (without carrying)
    e) convert the digts from the sums into letters


        a)    4vtvu 4eb44 utb4b 6baab deana tb
        b)    43032 45744 21747 67117 85191 07
        c)    32146 33752 72251 41211 26504 92
              --------------------------------
        d)    75178 78496 98998 08327 01695 99
        e)     HE L  LO ,    ~П   РИ  ВЕТ .  ~

The final result is: "HELLO, ~ПРИВЕТ.~"


Error Recovery:
--------------

It may be the case that a procedural error in the manual encryption process is made, and the sender accidently subtracts the key from the plain text like so:

Plain Text:   00000 75 1 78 78 49 69 89 9 80 8 3 27 01 6 95 99 
Random key: - 32146 33 7 52 72 25 14 12 1 12 6 5 04 92 8 31 09 110
              ----------------------------------------------------
              78964 42 4 26 06 24 55 77 8 78 2 8 23 19 8 64 90 990

This will be apparent when the first code group does not match the first group of the key.  The sender here has accidently "decrypted" their plain text into ciher text.  The receiver can process this using the encrypt process to retreive the clear text.  Mathematically, it is not wrong, but procedurally can lead to confusion if the receiver does not realise how easy it is to recover.



Message formats
---------------

Messages are usually composed of fields as follows:
1 - one digit message type code (0=Null (Nothing to send), 1=normal, 2=retransmission, 3=test, 4=key generation, 5=key compromise, 6=special announcement, 7=relay, 8=bulk data, 9=super-encrypted message )
2 - one digit region code (0=Any, 1=EU, 2=ME, 3=AF,4=SEA 5=NA, 6=SA, 7=AU/NZ, 8=RU, 9=CN)
3 - the recipients three digit station code
4 - three digit code count
5 - five digit key identifier
6 - five digit code groups
7 - 00000 sent three times to end transmission

The message type codes are used as follows:
0) The one way broadcast has no message for this transmission
1) Normal message follows
2) Transmissions are often resent within 24,48, or 72 hours later if there has been issues with radio propogation
3) Test transmission to be heard nearby by broadcast network test personnel who are also in range
4) A new set of keys are being sent, so get ready to copy them.  Likely to have had warning in a previous message.  Can be used to avoid depletion of keys when receiving bulk data.
5) Keys are compromised, destroy them, and revert to backup rekey procedure to generate a new set.
6) Special announcement sent to all stations using the reserved key shared by all regional operators.
7) Relay message to another operator within your local network.
8) Bulk data transmission is being sent, may have previously received key regneration request.
9) A special super encrypted message is being sent, decrypt using special keypad as previously instructed.


Example summary of SILENT DUCK (SD) commands:
---------------------------------------------

# Note: Comments prefixed by pound character "#" to make it easier to paste examples into the command line

# print out version number
otp -v

# print help
otp -h

# option: turn on use of More Shorts
otp -z ...

# option: turn on testing mode to inhibit modifying the file system (no write, delete, etc.)
otp -t ...

# keep files after use
otp -k .....

# throttle entropy depletion, a bit by sleeping for 'n' seconds after every five groups of five digits.  best to have other non-networking tasks running in backrgound to geneerate entropy.  Can try running the command: "watch cat /proc/sys/kernel/random/entropy_avail" to track the entropy available on Linux.
otp -n [1-999]

# set rounds of file wiping (writes 0, 1, random bits)
otp -r 3 ..... 

# specify number of times random data added to itself before return
# this slows down the process, and may deplete the entropy pool.  Useful for large key generation jobs that would deplete the entropy pool quickly anyways.
otp -q 3 ....

# generate key files, passing in a prefix for each of the 25 pages that will have the page number appended to it, ex: keys/XX123-01.otk
otp -g -y keys/XX123

# encipher file 
otp -e -i inputfile.txt -o outputfile.otp keys/XX123-01.otk
# same but specifying keys 5,6,7 (Linux/OSX/Android):
otp -e -i inputfile.txt -o outputfile.otp keys/XX123-0[5,6,7].otk

# decipher file (keys work same as above)
otp -d -i outputfile.otp -o cleartext.txt keys/XX123*.otk

# generate key for enciphered message based on known clear text
otp -f -c ciphertext.otp -p plaintext.txt -y newkey.otk

# use two key sheets to encipher a new random 25 sheet keypad
otp -j -i key01.otk -a key02.otk -o combinedkey.otk -y keys/ZZ456

# use two sheets to recover 25 sheet keypad
otp -u -i key01.otk -a key02.otk -c combinedkey.otk -y keys/AA567

# split and encipher message so we only need arbitrary minimum to deliver message
otp -s -i inputtext.txt -l 3 -x 5 splitA/AA-

# merge message and decipher using minimum aritrary message segments
otp -m -o outSegmentsMsg.txt splitA/*-123*

# combine two encoded streams together
otp -b -i input1.otk -a input2.otk -c combined -y keyPrefix

# wipe files
otp -w splitA/* keys/* *.ot? *.txt
