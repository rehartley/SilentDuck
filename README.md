# SilentDuck


Title:
------

SILENT DUCK - One time pad encryption processing sampler


Introduction:
-------------

This app automates a manual one time pad (OTP) encryption procedure field tested as far back as the 1970s and likely earlier. It has been publicly disclosed since early 1977.

SILENT DUCK (SD) is the name of the app, and is not the term used by the creators of this procedure.  SD automates manual one time pad (OTP) encryption of text, digits and some punctuation characters.

Although usable for encrypting messages, this app focuses on exercising various features particular to one time pad systems.

WARNING:
1) This app will automatically wipe and delete input files for each command.
2) Using the "-k" flag inhibits this auto-wipe and delete behaviour
3) There is also a test flag "-t" which prevents all file output

SILENT DUCK (SD) operates with the following Roman and Cyrillic letters, numerals, and punctuation marks:

```
A B C D E F G H I J K L M N O P Q R S T U V W X Y Z
А Б В Г Д Е Ж З И Й К Л М Н О П Р С Т У Ф Х Ц Ч Ш Щ Ы Ь Э Ю Я
0 1 2 3 4 5 6 7 8 9
? : @ / ~ # .
```

The space " ", and new line "\n", characters are included but hard to represent in print.

Missing from the Cyrillic alphabet are "Ё" ("yo") and "Ъ" ("твёрдый знак", or "tyordy znak", the hard sign)

Encrypted messages use letters converted to digits '0' through '9'.  When transmitting messages via Morse code, these can be represented with letters referred to as "morse shorts":
"t a u v 4 e 6 b d n"

History and pictures of artifacts at:
https://web.archive.org/web/20260821182405/https://www.numbers-stations.com/articles/trigon-numbers-station-the-case-of-alexandr-ogorodnik/


Manual OTP Procedure:
---------------------

The one time pad system is extremely simple.

Encryption of messages is performed by:

1) generating and sharing a series of random digits with the sender and recipient
2) converting letters of a message to digits
3) subtracting a series of pre-shared key random digits from those digits 
4) sending the result to the recipient
5) destroying the keys used.

To decrypt the message, the recipient will add the series of digits received to the random ones previously shared, and decode the results back into letters. At this point, the set of digits used is destroyed to avoid compromise.


Procedure:
----------

Before communication starts, a set of random numbers is generated for use as keys.  A copy is shared with the sender and the recipient.  These numbers must be completely random, and for best security, the only copies in existence should be the two held by the sender and the receiver.

In the historical one time pad, the series of numbers were arranged in five digit groups, eight groups to a line for forty lines to a page, and a single one time pad booklet might be around twenty pages long.  Size can vary among teams, but a commonly seen format is twenty five sheets of ten lines each holding five groups of five digits.

A typical page might start like this:

```
24765 93659 55146 09380 18882 67898 69598 95436
25341 88038 31282 39057 21708 51305 66499 20567
65096 02819 74377 27960 20471 53361 18687 06458
19226 31329 55134 83889 26588 24850 81322 87478
01334 80225 37061 13995 88827 07293 53021 81129
90865 91712 80927 18799 71311 57151 71976 06245
98890 61224 59636 08076 65747 36834 49525 92576
95428 50476 06584 38399 37155 75549 11968 12962
43041 83175 29737 88523 76769 29465 47144 75691
77230 19601 57378 51440 48030 63857 15846 37829
32548 48508 71999 22399 86499 22365 91365 74317
57311 83798 06280 74855 58916 46616 07784 57382
10464 00582 08702 30607 80017 50120 76361 88759
93610 38382 57828 27710 00947 00977 02927 89429
53217 20255 20839 63759 74408 60213 32159 73481
31617 14857 97505 25301 14258 36792 42161 05427
52190 32626 07392 88180 32382 22884 82072 81263
39585 92345 44974 09467 88114 50678 84634 02982
44347 73204 49702 60171 56691 11969 32188 62818
06460 37447 02998 93679 05391 96625 21874 88256
85784 28585 57163 61054 85038 41729 76885 51723
12105 61287 69331 72620 98079 56863 59622 96951
94389 88086 36174 39492 54706 56234 49308 07472
79967 13807 72543 07594 89680 63806 18102 32416
65413 91747 01977 31100 62600 78129 31020 07515
09685 11575 35283 37365 15236 28014 82731 07629
35772 51501 01308 09111 40637 41959 81825 82217
69421 13874 28982 52087 95908 43908 06689 55318
64308 31000 08437 64768 79907 58033 78288 44541
39151 31450 44942 53264 04459 19196 33063 68732
57000 78066 10301 31438 87160 08879 10617 39947
41192 47297 79960 45748 24756 60210 83200 78918
91761 48988 10844 64704 86812 61530 69324 30482
03174 79631 96669 88017 31989 32177 73058 80287
94449 59824 50666 22217 36665 78788 88951 51139
92675 67604 01497 28710 65505 37546 76036 64619
84157 68553 92307 42962 21660 78980 52154 40531
57646 07563 92053 84974 34262 59764 68318 44568
65986 82656 13413 64402 77821 46528 50300 34720
43525 90572 90038 01483 75550 94795 48699 55418
```

As messages are encrypted, the page with the keys used to encrypt them is destroyed to avoid discovery. The pads should be hidden securely while not in use, and ought to have some evidence of being tampered with such as perforation and wax/glue seals, colour shifting or fading inks that activate when exposed to water, heat, air or light, etc.

Letters are converted to digits using "straddling checkerboard" lookup tables.  These help reduce the size of the encrypted message that needs to be sent.  The seven most common letters are converted to the seven digits '0' through '6', and the remainder are converted to double digits in the range of 70 through 99 using the row in the table as the first digit, with the column number as the second digit.

The straddling checkboards for Latin and Cyrillic letters are shown here:

Latin Alphabet:

```
        0   1   2   3   4   5   6   7   8   9
        ======================================
        A   E   N   R   O   I   T   _   _   _
     7  B   C   G   D   F   H   J   K   L   M
     8  P   Q   S   U   V   W   X   Y   Z   _
     9  ?   :   @   /   #   .   ,   \n ' '  ~
```

Cyrillic Alphabet:

```
        0   1   2   3   4   5   6   7   8   9
        ======================================
        А   Е   И   Н   О   С   Т   _   _   _
     7  Б   В   Г   Д   Ж   З   Й   К   Л   М
     8  П   Р   У   Ф   Х   Ц   Ч   Ш   Щ   Ы
     9  Ь   Э   Ю   Я   #   .   ,   \n ' '  ~
```


Using the tables above, the English-Russian greeting "HELLO, ПРИВЕТ." is encoded as digits, using the tilde '~' as the alphabet swap code to switch between Latin and Cyrillic letters:

```
H   E   L   L   O   ,   ' '  ~   П   Р   И   В   Е   Т  .  ~
75  1   78  78  4   96  98   99  80  81  2   71  1   6  95 99
```


To encode digits, use the '#' to start and then double each digit, ending with a final '#'.  This way, the year '1985' encodes to  "#11998855#"

To generate encrypted text, subtract (without carrying) the encoded string of letters from the sample OTP key's digits. The digits are written out in the usual five-digit groups below the random key that was shared previously with the recipient, and then subtracted:

```
Plain Text:   75178 78496 98998 08127 11695 99
Random key: - 24765 93659 55146 09380 18882 67
              --------------------------------------
Difference:   51413 85847 43852 09847 03813 32
```

When messages are transmitted via a number station, they may use a common "Morse Shorts" table.  Morse code letters take less transmission time than numbers, and are easy to learn, since they are distinct and follow a pattern:

```
0 T -
1 A .-
2 U ..-
3 V ...-
4 4 ....-
5 E .
6 6 -....
7 B -...
8 D -..
9 N -.

0 1 2 3 4 5 6 7 8 9
T A U V 4 E 6 B D N
```

The final message ready for transmission becomes:

```
51413 85847 43852 09847 03813 32
EA4AV DED4B 4VDEU TND4B TVDAV VU
```


The message is now ready to send in Morse code.

On the recipient's end, the process is:

- a) transcribe the Morse
- b) convert to digits
- c) write the key value down
- d) sum the key values to the enciphered digits (without carrying)
- e) convert the digits from the sums into letters

```
a)    EA4AV DED4B 4VDEU TND4B TVDAV VU
b)    51413 85847 43852 09847 03813 32
c)    24765 93659 55146 09380 18882 67
      --------------------------------
d)    75178 78496 98998 08127 11695 99
e)     HE L  LO ,    ~П   РИ  ВЕТ .  ~
```

The final result is: "HELLO, ~ПРИВЕТ.~", or "HELLO, ПРИВЕТ." when we remove the "~" change alphabet artifacts.


Error Recovery:
--------------

It may be the case that a procedural error in the manual encryption process is made, and the sender accidentally performs the decryption step (adding the key) instead of the encryption step (subtracting it). A "AAAAA" check group is often added to the front of a message before encoding specifically to catch this kind of error, since it becomes a "00000" check group once encoded — the recipient expects to see it fall out on decryption, confirming the right key page was used:

```
Plain Text:   00000 75178 78496 98998 08127 11695 99
Random key: + 24765 93659 55146 09380 18882 67898 69
              --------------------------------------
Sent:         24765 68727 23532 97278 16909 78483 58
```

The recipient's normal decryption step (adding the key again) would now produce garbage instead of the expected "00000" check group, since what was sent isn't proper ciphertext at all — it's the plain text with the key added twice over. The receiver can recover the original message by instead treating the mistaken transmission as if it were plain text and running it through the normal encryption step (subtracting the key) a second time:

```
Received:     24765 68727 23532 97278 16909 78483 58
Random key: - 24765 93659 55146 09380 18882 67898 69
              --------------------------------------
Recovered:    00000 75178 78496 98998 08127 11695 99
```

The recovered digits begin with the "00000" check group again and decode back to "HELLO, ПРИВЕТ." Mathematically the error is not wrong, but procedurally it can lead to confusion if the receiver does not realise how easy it is to recover.

Message formats
---------------

As shared on some number station web sites, messages may be composed of fields that will vary depending on the sender, with examples such as:
 1) one digit message type code (0=Null (Nothing to send), 1=normal, 2=retransmission, 3=test, 4=key generation, 5=key compromise, 6=special announcement, 7=relay, 8=bulk data, 9=super-encrypted message)
 2) one digit region code (0=Any, 1=EU, 2=ME, 3=AF, 4=SEA, 5=NA, 6=SA, 7=AU/NZ, 8=RU, 9=CN)
 3) the recipient's three digit station code
 4) three digit code count
 5) five digit key identifier
 6) five digit code groups
 7) 00000 sent three times to end transmission

The message type codes are used as follows:
0) The one way broadcast has no message for this transmission
1) Normal message follows
2) Transmissions are often resent within 24, 48, or 72 hours later if there have been issues with radio propagation
3) Test transmission to be heard nearby by broadcast network test personnel who are also in range
4) A new set of keys are being sent, so get ready to copy them.  Likely to have had warning in a previous message.  Can be used to avoid depletion of keys when receiving bulk data.
5) Keys are compromised, destroy them, and revert to backup rekey procedure to generate a new set.
6) Special announcement sent to all stations using the reserved key shared by all regional operators.
7) Relay message to another operator within your local network.
8) Bulk data transmission is being sent, may have previously received key regeneration request.
9) A special super encrypted message is being sent, decrypt using special keypad as previously instructed.


Example summary of SILENT DUCK (SD) commands:
---------------------------------------------

```bash
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

# throttle entropy depletion, a bit by sleeping for 'n' seconds after every five groups of five digits.  best to have other non-networking tasks running in background to generate entropy.  Can try running the command: "watch cat /proc/sys/kernel/random/entropy_avail" to track the entropy available on Linux.
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

# merge message and decipher using minimum arbitrary message segments
otp -m -o outSegmentsMsg.txt splitA/*-123*

# combine two encoded streams together
otp -b -i input1.otk -a input2.otk -c combined -y keyPrefix

# wipe files
otp -w splitA/* keys/* *.ot? *.txt
```
