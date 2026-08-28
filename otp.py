#!/usr/bin/python3

# -*- coding: utf-8 -*-
# coding: cp1251

# ########################################### #
# Released under the BSD Zero Clause License  #
# ########################################### #

import sys
import os
import time
from itertools import combinations
import pytextedit

# Windows consoles (and PyInstaller-frozen exes running under them) often
# attach a legacy codepage like cp1252 to stdout/stderr, which cannot
# represent Cyrillic characters. Any print()/dbg() of Cyrillic text -- the
# "-hh" help listing, or a decoded Cyrillic message -- would then raise
# UnicodeEncodeError and crash the whole run instead of just printing wrong.
# Reconfigure both streams to UTF-8 up front so output is consistent
# regardless of the host codepage; errors='replace' means a genuinely
# incapable terminal degrades unprintable characters to '?' instead of
# crashing. hasattr guards both older stream types and --noconsole builds
# where sys.stdout/stderr can be None.
for _stream in (sys.stdout, sys.stderr):
    if hasattr( _stream, 'reconfigure' ):
        _stream.reconfigure( encoding='utf-8', errors='replace' )

'''SILENT DUCK (SD) encrypts messages with the manual one time pad (OTP) procedure used by the CIA to communicate privately with their Russian agents in the mid-1970s, who would perform this same procedure using pencil and paper.

It allows switching between Roman and Cyrillic alphabets, including numeric digits and some punctuation characters.

This same methodology has been in use by other countries' for their own similar communications.

OTP has traditionally been useful where enough keys for the maximum number of future mesages can be generated and shared ahead of time before being faced with a communications threat.

Prominent computer cryptographer Bruce Schneier summarised OTP in one of his blog posts at:
https://www.schneier.com/crypto-gram/archives/2002/1015.html#7

He ended his post as follows:
"One-time pads may be theoretically secure, but they are not secure in a practical sense. They replace a cryptographic problem that we know a lot about solving -- how to design secure algorithms -- with an implementation problem we have very little hope of solving. They're not the future. And you should look at anyone who says otherwise with deep and profound suspicion."

This author will not argue with him.

OTP offers interesting freedoms of communication.

This includes repudiation of messages (the ability to not be authoritatively quoted), as any OTP encrypted message can be "decrypted" to any other text, thus rendering any quotes as technically heresay.

In some ways, OTP is future proof and liberates the user from any communication technology more advanced than pen and paper. Unlike other cryptographic processing tools, OTP does not need to be a controlled item kept under lock and key.

SILENT DUCK (SD) allows enthusiasts to dabble with an essential element of spycraft in the comfort of their own home, but without having to sign the Official Secrets Act (OSA) or the Espionage Act, and without having to be a trained spy.

Please enjoy trying it out. You will find a few fun surprises along the way not explored in detail elsewhere. And if you do happen to be a wannabe spy, who knows, you may find it useful.'''

versionStr = 'v0.9h'

# globals
useMorseShorts = False
keepKeyFilesAfterUse = False
wipeRoundCount = 7

# when False (default/strict), encode() requires an explicit '#' digit-shift
# code around runs of digits and dies if one is missing. Set True to have
# encode() insert the missing '#' automatically instead of dying.
autoInsertDigitShiftCode = False

testingMode = False # inhibit changes to file system, no CUD (-R).

'''Sleeping parameters so we can give the OS a breather and gather more entropy'''
entropyGatheringSleepTime = 0 # zero is no sleep
fetchedEntropyCount = 0
fetchedEntropyQuota = 25 # five groups of five digits.

'''Iteration count for the number of times we fetch a random value and combine
it with another one.  This can "whiten" the key stream, but may deplete entropy
levels quicker, lumping the higher entropy consumption towards the start of the
generated key stream.  If doing lots of key generation, will deplete the entropy
quicker, so can set to a higher number.

Also, because it can slow down key generation appreciably, it feels like there
is more "compute" going on, and might impart more feelings of trust to results.

Feel free to ignore, since we have the key / code combine capability built-in. 
'''
RANDOM_DUPLICATES = 0

'''Historical TRIGON was longer: 5 digits X 8 groups X 40 liness per page, with
1600 digits per page 10 x 6 cm page.

To focus on shorter messages, with 25 sheet pads one sixth the size, composed of
random digit versions of:

01234 56789 01234 56789 01234
56789 01234 56789 01234 56789
01234 56789 01234 56789 01234
56789 01234 56789 01234 56789
01234 56789 01234 56789 01234
56789 01234 56789 01234 56789
01234 56789 01234 56789 01234
56789 01234 56789 01234 56789
01234 56789 01234 56789 01234
56789 01234 56789 01234 56789

Two of these are all we need to share to in order to transmit a new OTP key pad.

'''
DIGITSPERGROUP = 5
GROUPSPERLINE = 5
LINESPERPAGE = 10

SHEETSIZE = DIGITSPERGROUP * GROUPSPERLINE * LINESPERPAGE

PAGESPERPAD = 25
PADSIZE = SHEETSIZE * PAGESPERPAD


_print_debug_msg_ = True

def dbg( s ):
    if _print_debug_msg_:
        print( s )

def die( s ):
    dbg( s )
    sys.exit(1)


# read a file, return contents as a string
def readFile( fn ):
    if fn == 'EDITOR':
        return pytextedit.editString( filename='EDITOR' )

    tmp = ''
    try:
        f = open( fn, 'r' , encoding="utf-8")
        tmp = f.read()
        f.close()
    except:
        die( 'Error reading file: ' + str(fn) )
    
    return tmp

# append string to end of file
def writeFile( fn, s ):
    global testingMode

    if fn == 'EDITOR':
        pytextedit.editString( string=s, filename='EDITOR' )
        return

    if testingMode == True:
        dbg( 'NOTE: TESTING MODE: writeFile() will not write to file: ' + fn )
        return
        
    try:
        parentDirectory = os.path.dirname( fn )
        if parentDirectory:
            os.makedirs( parentDirectory, exist_ok=True )
        f = open( fn, 'w+', encoding="utf-8" )
        f.write( s )
        f.flush()
        os.fsync( f )
        os.fsync( f )
        os.fsync( f )
        f.close()
    except:
        die( '\nERROR: writing file: ' + fn + ' with string:\n' + s + '\n' )
# writeFile()


combo5x10 = [ # m: 5, n: 10, 252 combinations 
    "01234", "01235", "01236", "01237", "01238", 
    "01239", "01245", "01246", "01247", "01248", 
    "01249", "01256", "01257", "01258", "01259", 
    "01267", "01268", "01269", "01278", "01279", 
    "01289", "01345", "01346", "01347", "01348", 
    "01349", "01356", "01357", "01358", "01359", 
    "01367", "01368", "01369", "01378", "01379", 
    "01389", "01456", "01457", "01458", "01459",
    "01467", "01468", "01469", "01478", "01479", 
    "01489", "01567", "01568", "01569", "01578", 
    "01579", "01589", "01678", "01679", "01689",
    "01789", "02345", "02346", "02347", "02348", 
    "02349", "02356", "02357", "02358", "02359", 
    "02367", "02368", "02369", "02378", "02379", 
    "02389", "02456", "02457", "02458", "02459", 
    "02467", "02468", "02469", "02478", "02479", 
    "02489", "02567", "02568", "02569", "02578", 
    "02579", "02589", "02678", "02679", "02689", 
    "02789", "03456", "03457", "03458", "03459",
    "03467", "03468", "03469", "03478", "03479", 
    "03489", "03567", "03568", "03569", "03578", 
    "03579", "03589", "03678", "03679", "03689", 
    "03789", "04567", "04568", "04569", "04578", 
    "04579", "04589", "04678", "04679", "04689", 
    "04789", "05678", "05679", "05689", "05789",
    "06789", "12345", "12346", "12347", "12348", 
    "12349", "12356", "12357", "12358", "12359",
    "12367", "12368", "12369", "12378", "12379",
    "12389", "12456", "12457", "12458", "12459",
    "12467", "12468", "12469", "12478", "12479",
    "12489", "12567", "12568", "12569", "12578",
    "12579", "12589", "12678", "12679", "12689",
    "12789", "13456", "13457", "13458", "13459",
    "13467", "13468", "13469", "13478", "13479",
    "13489", "13567", "13568", "13569", "13578",
    "13579", "13589", "13678", "13679", "13689",
    "13789", "14567", "14568", "14569", "14578",
    "14579", "14589", "14678", "14679", "14689",
    "14789", "15678", "15679", "15689", "15789",
    "16789", "23456", "23457", "23458", "23459",
    "23467", "23468", "23469", "23478", "23479",
    "23489", "23567", "23568", "23569", "23578",
    "23579", "23589", "23678", "23679", "23689",
    "23789", "24567", "24568", "24569", "24578",
    "24579", "24589", "24678", "24679", "24689",
    "24789", "25678", "25679", "25689", "25789",
    "26789", "34567", "34568", "34569", "34578",
    "34579", "34589", "34678", "34679", "34689",
    "34789", "35678", "35679", "35689", "35789",
    "36789", "45678", "45679", "45689", "45789",
    "46789", "56789"
] # combo5x10 table

# given a number, what letter is TAUV4E6BDN'

ignoreChars = ' \n'

morseCutLetter = { '0':'T', '1':'A', '2':'U', '3':'V', '4':'4',
                   '5':'E', '6':'6', '7':'B', '8':'D', '9':'N' }

# given a letter, what number is it
morseCutNumber = { 'T':'0', 'A':'1', 'U':'2', 'V':'3', '4':'4',
                    'E':'5', '6':'6', 'B':'7', 'D':'8', 'N':'9' }


def toMorseCut( s ):
    if useMorseShorts == False:
        return s
    tmp = ''
    for ch in s:
    
        if ch in ignoreChars:
            continue
            
        try:
            tmp += morseCutLetter[ch]
        except:
            die( 'Invalid character "' + ch + '" encountered converting to Morse cut.' )
    return tmp



def fromMorseCut( s ):
    if useMorseShorts == False:
        return s
    s = s.upper()
    tmp = ''
    for ch in s:
        if ch in ignoreChars:
            continue
        try:
            tmp += morseCutNumber[ ch ]
        except:
            die( 'Invalid character "' + ch + '" encountered converting from Morse cut.' )
    return tmp



# list of roman and cyrillic letters of the alphabet we will process
validStr = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ?:@/~#., \n0123456789АБВГДЕЖЗИЙКЛМНОПРСТУФХЦЧШЩЫЬЭЮЯ'

# return true if all characters are in validStr above
def stringValid( s ):
    s = s.upper()
    for x in range( len(s) ):
        if not s[x] in validStr:
            return False
    return True


# extract only the digits of a string, ignore the rest, which means it is 
# easy to document as long as commetns contain no digits.
def stringDigits( s ):
    tmp = ''
    for x in range( len(s) ):
        ch = s[x]
        if ch.isdigit():
            tmp+= ch
    return tmp


def loadKeyPad( fn ):
    tmp = readFile( fn )
    tmp = stringDigits( tmp )
    return tmp


# straddling checkerboard allows reduced message size since most common
# letters are encoded using only single digits.

digit_code = '94'
numberCodes =  ['00','11','22','33','44','55','66','77','88','99']

# this is our number to letter lookup table for Roman letters
latletter2numberTbl = {}

number2latletterTbl = {
    # single digits
    '0':'A', '1':'E', '2':'N', '3':'R', '4':'O', '5':'I', '6':'T',
    # double digits
    '70':'B', '71':'C', '72':'G', '73':'D', '74':'F',
    '75':'H', '76':'J', '77':'K', '78':'L', '79':'M',
    '80':'P', '81':'Q', '82':'S', '83':'U', '84':'V',
    '85':'W', '86':'X', '87':'Y', '88':'Z',
    # 89 reserved / unassigned (was going to be a raw code char, followed by
    # digit count of four digits, but that was never implemented -- leave
    # unmapped so decode() reports it instead of silently dropping it)
    '90':'?', '91':':', '92':'@', '93':'/',

    # 94 11 22 33 94 = '123'
    '94':'#', # digit shift code double digits then another shift code
    '95':'.',
    '96':',',
    '97':'\n',
    '98':' ',
    '99':'~' }

# cyrillic section
cyrletter2numberTbl = {}

number2cyrletterTbl = {
    # АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ
    # single digits
    '0':'А', '1':'Е', '2':'И', '3':'Н', '4':'О', '5':'С', '6':'Т',
    # double digits
    '70':'Б', '71':'В', '72':'Г', '73':'Д', '74':'Ж',
    '75':'З', '76':'Й', '77':'К', '78':'Л', '79':'М',
    '80':'П', '81':'Р', '82':'У', '83':'Ф', '84':'Х',
    '85':'Ц', '86':'Ч', '87':'Ш', '88':'Щ', '89':'Ы',
    # 
    '90':'Ь', '91':'Э', '92':'Ю', '93':'Я', 

    # 94 11 22 33 94 = '123'
    '94':'#', # digit shift code double digits then another shift code
    '95':'.',
    '96':',',
    '97':'\n',
    '98':' ',
    '99':'~'  # tilde is our cyr lat shift code
}

def tblSetup():
    for x in number2latletterTbl:
        ch = number2latletterTbl[x]
        latletter2numberTbl[ ch ] = x

    for x in number2cyrletterTbl:
        ch = number2cyrletterTbl[x]
        cyrletter2numberTbl[ ch ] = x


# scan a string and insert the '~' alphabet-switch code wherever the
# alphabet in use needs to change, so callers don't have to hand-annotate
# every switch themselves -- "HELLO, ПРИВЕТ." works the same as
# "HELLO, ~ПРИВЕТ~.", including a leading switch if the message doesn't
# open in the Roman alphabet. Characters shared by both alphabets (digits,
# space, newline, punctuation, '#') never force a switch; an explicit '~'
# already present is tracked rather than duplicated, so already-annotated
# messages are unaffected.
def insertAlphabetSwitches( s ):
    tmp = ''
    usingRoman = True

    for ch in s:
        if ch == '~':
            usingRoman = (usingRoman == False)
            tmp += ch
            continue

        inLat = ch in latletter2numberTbl
        inCyr = ch in cyrletter2numberTbl

        if inLat and not inCyr and not usingRoman:
            tmp += '~'
            usingRoman = True
        elif inCyr and not inLat and usingRoman:
            tmp += '~'
            usingRoman = False

        tmp += ch

    return tmp


def codeGroups( s, groupSize=DIGITSPERGROUP, groupsPerLine=GROUPSPERLINE, linesPerPad=LINESPERPAGE ):
    tmp = ''

    lineCount = 0
    groupCount = 0
    
    for x in range( 0, len(s), groupSize ):
        group = s[x:x+groupSize]
        tmp += group + ' '

        groupCount += 1

        if groupCount >= groupsPerLine:
            tmp += '\n'
            groupCount = 0
            lineCount += 1
            
            if lineCount >= linesPerPad:
                tmp += '\n'

                lineCount = 0
    return tmp        


# optionally sleep for a number of seconds between 25 digit blocks.
def entropySleep():
    global entropyGatheringSleepTime
    
    # check if we need to sleep for entropy gathering
    if entropyGatheringSleepTime > 0:
        # only reference these inside this loop as an optimization
        # since we want to keep this loop relatively tight
        global fetchedEntropyCount
        global fetchedEntropyQuota

        fetchedEntropyCount += 1
        
        if fetchedEntropyCount > fetchedEntropyQuota:
            time.sleep( entropyGatheringSleepTime )
            fetchedEntropyCount = 0
        else:
            pass
    else:
        pass

'''
Notes about random data:
https://docs.python.org/3/library/os.html

On Windows, os.urandom() internally uses CryptGenRandom(), which uses RC4,
and this is said to be infrequently reseeded.
- https://docs.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-cryptgenrandom
- https://en.wikipedia.org/wiki/CryptGenRandom

On Linux we have /dev/random
- https://en.wikipedia.org/wiki//dev/random

So, in the end, this data is NOT random in the religious sense followers of
the Church of Randomology would prefer, but it has great potential to be
unpredictable enough .

'''
# fetch a single digit in the range of '0' to '9'
def randDigit():

    entropySleep() # give our OS's entropy gathering a breather.
            
    tmp = 10    
    while (tmp>9):
        tmp = ord( os.urandom(1) )
        if tmp < 250:
            # keep 0..249, discard 250..255 to avoid bias
            tmp = tmp % 10
    return str(tmp)

def getRandDigits( dc ):
    tmp = ''

    for x in range( dc ):
        tmp += randDigit()
    return tmp

# invoke the above a certain number of times, adding the strings of numbers
# together to help 'whiten' the digits origin.
def randDigits( dc ):
    tmp = getRandDigits( dc )
    
    '''
    # bad idea - we deplete entropy quickly and revert our stream to a PRNG
    # better to do same redundancy later, with entropy spread around
    for i in range( 1 + RANDOM_DUPLICATES ):
        tmp = stringAdd( tmp, getRandDigits( dc ) )  
    '''
    return tmp


# add a to b, with result the length of a
def stringAdd( a, b ):
    if len(a) > len(b):
        die( 'stringAdd( a, b ): len(a) > len(b) by ' + str( len(a)-len(b) ) + ' characters.')

    tmp = ''

    for x in range( len(a) ):
        d = int(a[x]) + int(b[x])
        if d>9:
            d-=10
        tmp += str( d )

    return tmp



def stringSubtract( a, b ):
    if len(a) > len(b):
        die( 'stringSubtract( a, b ): len(a) > len(b) by ' + str( len(a)-len(b) ) + ' characters.' )

    tmp = ''

    for x in range( len(a) ):
        d = int(a[x]) - int(b[x])
        if d<0:
            d+=10
        tmp += str( d )
    return tmp


# convert letters of a string into digits using tables above
def encode( strIn ):
    '''Simplify by iterating through string chars:
    - convert to upper case
    - add digit code as needed
    - double digits
    - if different alphabet, add '~' between shifts
    '''
    usingRoman = True
    usingDigits = False
    s = strIn.upper()
    tmp = ''

    currentletter2numberTbl = latletter2numberTbl

    if not stringValid( s ):
        dbg( 'invalid characters in string: ' + s )
        die( 'invalid characters in string: ' + s )

    s = insertAlphabetSwitches( s )
    lenS = len( s )

    x=0
    
    while x < lenS:
        ch = s[x]
        
        if ch == '#': # 94
            usingDigits = (usingDigits == False)
            tmp += digit_code
        elif ch == '~': # 99
            tmp += '99'
            usingRoman = (usingRoman == False)
            
            if usingRoman:
                currentletter2numberTbl = latletter2numberTbl
            else:
                currentletter2numberTbl = cyrletter2numberTbl
        elif ch.isdigit():
            # process digits
            if not usingDigits:
                if autoInsertDigitShiftCode:
                    usingDigits = True
                    tmp += digit_code # '94'
                else:
                    die( 'missing digit code "#" at start of number sequence.' )
            tmp += ch+ch
        else:
            # process all else
            if usingDigits:
                if autoInsertDigitShiftCode:
                    usingDigits = False
                    tmp += digit_code
                else:
                    die( 'Missing "#" code to end number sequence.' )

            try:
                tmp += currentletter2numberTbl[ ch ]
            except:
                # NOTE: don't dbg()/print the lookup table itself here -- it can
                # contain Cyrillic characters, and printing those on a console
                # whose codepage can't represent them (e.g. Windows cp1252)
                # raises UnicodeEncodeError, masking the real error below.
                alphabetName = 'Roman' if usingRoman else 'Cyrillic'
                die( 'Letter "' + ch + '", code: ' + str(ord(ch)) + ', not in the ' + alphabetName + ' alphabet - missing "~" ?' )
    
        x += 1
    # end of while
    
    if usingDigits:
        if autoInsertDigitShiftCode:
            tmp += '94'
        else:
            die( 'Missing final "#" code to end number sequence.' )

    if usingRoman == False:
        # this lets us concatenate multiple messages regardless of alphabet used
        tmp += '99'
    
    return tmp

# convert a series of digits into a series of letters using a straddling checkerboard
def decode( strIn ):
    s = stringDigits( strIn )
    lenS = len( s )
    usingRoman = True
    tmp = ''
    code = ''
    ch = ''
    x = 0

    # allows switching out letter tables
    number2letterTbl = number2latletterTbl
    
    while x < lenS:
        code = s[x]
        if code > '6':
            # double digits
            x+=1
            
            if x < lenS:
                code += s[x]
            else:                
                die( 'string missing a character in encode(): ' + strIn )
                
        # two special codes '94' for digits, '99' for alphabet switch
        if code =='94':
            tmp += '#'
            x+=1
            # process digits until none left
            code = ''
            while not code == '94':
                if lenS < (x+2):
                    dbg( 'code: ' + code )
                    die( 'numbers are short a digits: ' + strIn )
                code = s[x:x+2]

                if code in numberCodes:
                    tmp += code[0]
                elif code == '94':
                    x -= 1
                else:
                    dbg( 'tmp: ' + tmp )
                    die( 'error: decode() - ' + strIn )
                x+=2
            tmp += '#'

        elif code == '99':
            # swap alphabets -- '~' is a control code, not part of the
            # message, so it's tracked here but not written to the output;
            # this mirrors encode()'s insertAlphabetSwitches() on the way
            # in, so decoded text comes back clean without the caller
            # having had to type the switches themselves.
            usingRoman = (usingRoman == False)
            if usingRoman:
                number2letterTbl = number2latletterTbl
            else:
                number2letterTbl = number2cyrletterTbl
        else:
            try:
                ch = number2letterTbl[ code ]
            except KeyError:
                die( 'code "' + code + '" is not assigned to any letter in this alphabet: ' + strIn )
            tmp += ch

        x += 1

    return tmp

def init():
    tblSetup()


# ####################################################################
# command processing starts here

def do_version():
    die( 'SILENT DUCK - OTP (one time pad) version: ' + versionStr +
         ',\nCopyright (c) July 1994, released under Creative Commons (CC).' )


def do_brief_help():
    briefHelpStr = '''
-v print out version number
-h print help
-z option: turn on use of More Shorts
-t option: turn on testing mode to inhibit modifying the file system (no write, delete, etc.)
-k keep input files after use to avoid auto-destruction
-n throttle entropy consumption sleeping for 'n' seconds after every 25 digits.
-r set number of rounds for file wiping (writes 0, 1, random bits)
-q specify number of times random data added to itself before return
-g generate key files
-gz generate all-zero key files (testing only -- NEVER use for a real message)
-e encipher file, wiping input file and keys
-d decipher file, wiping input file and keys
-f generate key for previouly enciphered message based on known clear text
-j use two key sheets to encipher a new random 25 sheet keypad, deleting input files
-u use two key sheets to recover 25 sheet keypad, deleting input files
-s split and encipher message so we only need arbitrary minimum to deliver message, delets input files
-m merge message and decipher using minimum aritrary message segments, deleting input files
-b combine two encoded streams together, deleting after new stream is output
-w wipe files

Use the filename "EDITOR" in place of any -i/-o/-c/-p/-a/-y file argument (or
a key filename) to type/read that text on screen via a built-in full-screen
editor instead of touching the file system.

-hh helpful help with more text.

'''
    die( briefHelpStr )


def do_help():
    helpStr='''
This app automates manual one time pad (OTP) encryption of text, digits and some punctuation characters.


*********************************************************
* W A R N I N G   -   W A R N I N G   -   W A R N I N G *
*********************************************************

1) In the interests of security, input files are wiped and erased from the file system.
2) You may use the "-k" option to indicate that input files are to be kept.
3) The "-t" option may also be used to indicate that no output files will be created.



Reading/writing text without touching the file system:

Instead of a real filename, pass the special name "EDITOR" to any -i, -o, -c,
-p, -a, or -y file argument (or as a key filename) to bring up a built-in
full-screen text editor:

  - As an input (-i, -c, -p, -a, or a key file), "EDITOR" opens an empty
    editor for you to type/paste text into; the buffer's contents are used
    in place of a file's contents when you exit (F2 / Ctrl+S).
  - As an output (-o, -y, -c), "EDITOR" displays the resulting text (e.g.
    deciphered plaintext) full-screen instead of writing it to a file;
    nothing is saved back, so it never touches the file system.

This lets you encipher/decipher messages, or view decrypted output, without
ever writing plaintext to disk. "EDITOR" is also skipped by the automatic
file-wiping step, since there is no file on disk to wipe.



It operates with the following Roman and Cyrillic letters, numberals, and punctuation marks:

A B C D E F G H I J K L M N O P Q R S T U V W X Y Z
А Б В Г Д Е Ж З И Й К Л М Н О П Р С Т У Ф Х Ц Ч Ш Щ Ы Ь Э Ю Я
0 1 2 3 4 5 6 7 8 9
 ? : @ / ~ # .
 
The space " ", and new line "\n", characters are included but hard to represent in print.

Missing from the Cyrillic alphabet are "Ё" ("yo"), "Ъ" ( "твёрдый знак", or "tyordy znak", the hard sign)

Encrypted messages use letters converted to digits zero through nine which can optionally be represented for easier transmission in Morse code with these ten letters:
"t a u v 4 e 6 b d n"


Further background at:
http://users.telenet.be/d.rijmenants/en/onetimepad.htm
    and
https://www.numbers-stations.com/articles/trigon-numbers-station-the-case-of-alexandr-ogorodnik/



OTP commands and options:

NOTE:
1) need test function so we can see if we need more keys for a message
2) set quotas on random bytes and add sleep periods

# -v print out version number
otp -v

# -h print help
otp -h

# -z option: turn on use of More Shorts
otp -z ...

# -t option: turn on testing mode to inhibit modifying the file system (no write, delete, etc.)
otp -t ...

# -k keep key files after use
otp -k .....

# -n throttle entropy consumption sleeping for 'n' seconds after every 25 digits.
# Have non-networking tasks running in backrgound to geneerate entropy.
# Run the Linux command: "watch cat /proc/sys/kernel/random/entropy_avail" to track estimated entropy levels
otp -n [1-999]

# -r set number of rounds for file wiping (writes 0, 1, random bits)
otp -r 3 ..... 

# -q specify number of times random data added to itself before return
# this slows down the process, and may deplete the entropy pool.  Useful for large key generation jobs that would deplete the entropy pool quickly anyways.
otp -q 3 ....

# -g generate key files
# provide filename prefix for the 25 pages with page number appended, ex: keys/XX123-01.otk
otp -g -y keys/XX123

# -gz generate all-zero key files -- same as -g, but skips real randomness
# and writes sheets of all zeros instead. Testing only: an all-zero key
# provides no security at all (ciphertext comes out equal to plaintext).
# NEVER use for a real message.
otp -gz -y keys/XX123

# -e encipher file
otp -e -i inputfile.txt -o outputfile.otp keys/XX123-01.otk
# same but specifying keys 5,6,7 (Linux/OSX/Android):
otp -e -i inputfile.txt -o outputfile.otp keys/XX123-0[5,6,7].otk
# type the plaintext on screen instead of reading a file:
otp -e -i EDITOR -o outputfile.otp keys/XX123-01.otk

# -d decipher file
# (keys work same as above)
otp -d -i outputfile.otp -o cleartext.txt keys/XX123*.otk
# view the deciphered plaintext on screen instead of writing a file:
otp -d -i outputfile.otp -o EDITOR keys/XX123*.otk

# -f generate key for previouly enciphered message based on known clear text
otp -f -c ciphertext.otp -p plaintext.txt -y newkey.otk

# -j use two key sheets to encipher a new random 25 sheet keypad
otp -j -i key01.otk -a key02.otk -o combinedkey.otk -y keys/ZZ456

# -u use two key sheets to recover 25 sheet keypad
otp -u -i key01.otk -a key02.otk -c combinedkey.otk -y keys/AA567

# -s split and encipher message so we only need arbitrary minimum to deliver message
otp -s -i inputtext.txt -l 3 -x 5 splitA/AA-

# -m merge message and decipher using minimum aritrary message segments
otp -m -o outSegmentsMsg.txt splitA/*-123*

# -b combine two encoded streams together
otp -b -i input1.otk -a input2.otk -c combined -y keyPrefix

# -w wipe files
otp -w splitA/* keys/* *.ot? *.txt
'''

    die( helpStr )


# generate keypad using a file prefix. zeroKeys=True (the "-gz" command)
# skips the random generation entirely and writes all-zero sheets instead --
# NEVER for real messages (an all-zero key is no key at all, the ciphertext
# comes out equal to the plaintext), only for testing the tool itself
# (matches the otp_test/zero.otk fixture) where predictable, reviewable
# output is more useful than real entropy.
def do_keygen( prefix, zeroKeys=False ):
    if prefix is None:
        die( 'no key prefix given')

    global DIGITSPERGROUP # = 5
    global GROUPSPERLINE # = 10 # 5
    global LINESPERPAGE # = 50 # 10


    global SHEETSIZE # = DIGITSPERGROUP * GROUPSPERLINE * LINESPERPAGE

    global PAGESPERPAD # = 100 # 25
    global PADSIZE # = SHEETSIZE * PAGESPERPAD

    for x in range( 1, 1 + PAGESPERPAD ):
        fn = prefix + '-' + str( "{:03d}".format(x) ) + '.otk'

        if zeroKeys:
            tmp = '0' * SHEETSIZE
        else:
            '''
            Lets hope there will be enough entropy inserted into the random
            stream that the OS's PRNG will be sufficient.  We know we will
            run out of entropy, hopefully where that happens will be random
            enough, and overlayed / whitened enough by other data overlaid
            on top of it.
            '''
            tmp = '0' * SHEETSIZE
            for i in range( 1 + RANDOM_DUPLICATES ):
                tmp = stringAdd( tmp, randDigits( SHEETSIZE ) )

        writeFile( fn, codeGroups( tmp ) )


def loadKeys( keyList ):
    key = ''
    for k in keyList:
        kfn = keyList[k]
        key += readFile( kfn )
    key = stringDigits( key )
    return key
    
def wipeKeys( keyList ):
    if keepKeyFilesAfterUse:
        dbg( 'WARNING: Keys being kept after use!' )
        return
        
    for k in keyList:
        kfn = keyList[k]
        wipeFile( kfn )
     


def do_encipher( inputFilename, outputFilename, keyList ):
    key = loadKeys( keyList )
    
    inputTxt = readFile( inputFilename )
    inputTxt = inputTxt.upper()
    encodedTxt = encode( inputTxt )
    
    # decodedTxt = decode( encodedTxt )
    # dbg( 'Encoded:\n' + codeGroups( encodedTxt) )
    # dbg( 'Decoded:\n' + decodedTxt )
    
    # the below is different from other people's examples, but it means that we
    # can prefix our message to "AAAAA" and it gets converted to "00000" and it
    # becomes the the clear text of the first code block used for encryption
    # so it is an easy way to confirm the keypad to use.
    cipherTxt = stringSubtract( encodedTxt, key ) # OPSEC: write key, then text
    
    cgTxt = codeGroups( toMorseCut( cipherTxt ) )
    writeFile( outputFilename, cgTxt )
    
    wipeKeys( keyList )  

    if keepKeyFilesAfterUse:
        dbg( 'WARNING: Plain text being kept after encryption!' )
    else:
        wipeFile( inputFilename )



def do_decipher( inputFilename, outputFilename, keyList ):
    key = loadKeys( keyList )
    
    inputTxt = readFile( inputFilename )
    inputTxt = fromMorseCut( inputTxt )
    inputTxt = stringDigits( inputTxt )
    
    # see long rambling note above in "do_encipher()"
    clearTxt = stringAdd( inputTxt, key ) # OPSEC: write cipher text, then key
    
    # dbg( codeGroups( clearTxt ) )
    
    decodedTxt = decode( clearTxt )
    writeFile( outputFilename, decodedTxt )    
    wipeKeys( keyList )

    if keepKeyFilesAfterUse:
        dbg( 'WARNING: Plain text being kept after use!' )
    else:
        wipeFile( inputFilename )



def do_fakeMsg( cipherTextFile, plainTextFile, keyFile ):
    '''Generate key text so the ciphertext of a plain text matches another
    ciphertext
    '''
    # encode the plain text
    msgP = encode( readFile( plainTextFile ) )
    
     #read the cipherText, convert from morse cut to digits
    msgC = stringDigits( fromMorseCut( readFile( cipherTextFile ) ) )
    
    # compare lengths:
    if len( msgC ) != len( msgP ):
        dbg( 'ciphertext len: ' + str( len( msgC ) ) )
        dbg( 'plaintext len:  ' + str( len( msgP ) ) )
        die( 'Error: cipher text and encoded plain text inputs must be the same length.' )


    kStr = ''
    kStr = stringSubtract( msgP, msgC )
    kStr = codeGroups( kStr )
    writeFile( keyFile, kStr )


# ############################################################################
# ############################################################################
# ############################################################################



'''
keygencombine( i, j, o, prefix )

Generate and combine a new keypad.

Input: 500 random digits (2 sheets x 10 lines x 5 x 5) previously shared with recipient (two sheets of OTP)
Output: 12,500 digits (100 minutes at 25 WPM): 50 sheets x 10 lines x 5 groups x 5 digits

Provides a 25:1 ratio

   - generate ci and cj from all 252 combinations of the input ki and kj - sums of five of the ten lines at a time.
   - create a new checksum column which for ci and cj, adding five correpsonding digits to get a sixth columns
   - create kc by putting two tables together and sorting on the sixth column
   - generate new double sized random keypad, kr
   - encrypt kr with kc to generate ct
   - send ct to recipient

   - return: ct

'''

# input: a string of 250 digits
# split into a table of 10 rows
# do combinations on them
# append checksum composed of each five digit group to each row
# return table of ten rows

def combinateExpandedKeys( keyInput ):
    otp_tmp = {}
    s = ""
    checkSumString = ''
    i = 0
    j = 0
    r = 0
    retVal = {}
   
    # code is meant to be easy to read and understand by new Lua coders, it it is not meant to be optimised.
   
    # put otp into table indexed by row
    otp_tmp['0'] = keyInput[ 0 : 25]
    otp_tmp['1'] = keyInput[ 25: 50]
    otp_tmp['2'] = keyInput[ 50: 75]
    otp_tmp['3'] = keyInput[ 75:100]
    otp_tmp['4'] = keyInput[100:125]
    otp_tmp['5'] = keyInput[125:150]
    otp_tmp['6'] = keyInput[150:175]
    otp_tmp['7'] = keyInput[175:200]
    otp_tmp['8'] = keyInput[200:225]
    otp_tmp['9'] = keyInput[225:250]
   
    # ---------------------

    for combo in combo5x10:
        #    0000000001111111111222222
        #    1234567890123456789012345
        s = '0000000000000000000000000'
      
        for index in combo:
            # add the string here to make it easier to undo by hand.
            # assumption is that addition is easier to do in the head
            # than subtraction
            s = stringSubtract( s, otp_tmp[ index ] )

        # check sum
        # use addition here because it will be redone, not undone.
        checkSumString = s[0:5]
        checkSumString = stringAdd( checkSumString, s[ 5:10] )
        checkSumString = stringAdd( checkSumString, s[10:15] )
        checkSumString = stringAdd( checkSumString, s[15:20] )
        checkSumString = stringAdd( checkSumString, s[20:25] )
        
        tmp = s + checkSumString
        
        retVal[ len(retVal) ] = tmp

    return retVal



'''
input parameters:
    - two key files
    - an output file
    - file prefix for saving 25 new random keypad sheets
  process:
    take our last two otp sheets
    generate 252 combinations of five row sums (where each row has checksum appended to it)
    merge the second to the first
    sort by checksum
    generate new random key
    merge the key with the new random key to generate a new combined key
  
  Question: Do we need to use both double pads and checksum randomizing?
  
  Answer: Could get rid of the checksum randomizing, and keep two pad feature.
  One benefit of there being 252 combinations is that the last two rows of the
  first pad are now the first two rows of the second.  This means we avoid the
  two pads being the simple sum of each other when joined up.

We enable the checksum randomizing option. The main issue is that it is more difficult to do on pencil and paper, but will require experimentation to be sure.

It may be trivial compared to the 12600 (504 * 5 * 5) numeric additions to make
the keypad combinations, plus 1500 more to add the two halves of the random key.

This could be done using a squared ruled "quadpad", or a spreadsheet or even
this bit of code using an offline computer or dedicated device.

'''

def do_joinKeys( file_i, file_j, combinedKeyFile, prefix ):
    if prefix is None:
        die( 'no key prefix given')
   
    ki = loadKeyPad( file_i )
   
    if( len(ki) != SHEETSIZE ):
        dbg( str(len(ki)) + ', ki:' + ki )
        die( 'bad first key:')
   
    kj = loadKeyPad( file_j )
   
    if( len(kj) != SHEETSIZE ):
        dbg( str(len(kj)) + ', ki:' + kj )
        die( 'bad second key:')
   
    iTmp = combinateExpandedKeys( ki )   
    jTmp = combinateExpandedKeys( kj )   
    ki = None
    kj = None
   
    # append the two tables together, so we have 504 rows of 30 digits each (25 + 5 checksum).
    # combinateExpandedKeys() returns a dict keyed 0..251 in combo5x10 order, not a list,
    # so pull the values out in that order before concatenating -- dicts don't support "+".
    tmp = [ iTmp[c] for c in range( len(iTmp) ) ] + [ jTmp[c] for c in range( len(jTmp) ) ]
    iTmp = None
    jTmp = None

    # create dictionary indexed by the unique checksum
    keys = {}
    x = ''

    checkSumString = '00000'
    csNum = 0
    
    # build table indexed by checksum
    for x in tmp:
        # take checksum / index added previously as row's last five digits
        checkSumString = x[-5:]

        # remove five digit checksum from string x
        x = x[:-5:]

        # find next empty spot by avoiding hash/checksum collisions.
        # wrap mod 100000 (take the rightmost 5 digits) instead of just
        # adding 1, so checkSumString always stays exactly five digits --
        # otherwise a collision chain crossing '99999' would overflow to
        # a 6-digit string like '100000', which a plain string sort
        # would misorder ahead of '99999'.
        while( checkSumString in keys ):
            csNum = ( int(checkSumString) + 1 ) % 100000
            checkSumString = '{:05}'.format(csNum )

        # dictionary of OTP keys indexed by checksum
        keys[checkSumString] = x

    # keys is now a dict of 504 rows of 25 digits each, indexed by checksum. The
    # checksum is not stored in the value, only in the key, so it is not
    # available to an attacker who sees the ciphertext, and it is not needed
    # for decryption, since both ends can recompute it from the two input pads.
    tmp = None
    tmp = []

    sorted_keys = sorted(keys.keys())

    for k in sorted_keys:
        tmp.append( keys[k] )
    
    keys = None

    keys_size = len(tmp)
    half_keys_size = int( keys_size / 2 )

    # the keys are interwoven, so we split the 504 rows into two halves of 252 rows each, and then combine them with a random 25 sheet keypad to generate a new combined key

    keysA = tmp[:half_keys_size]
    keysB = tmp[half_keys_size:]
    tmp = None

    newOtpTbl = ''
    newRandomKeyStr = ''

    # combine the three parts
    for x in range(half_keys_size):
        tmp_s = stringAdd(keysA[x], keysB[x])
        # this is what is different between receiver and sender: the fresh
        # random pad material (rd) has to be kept, not just folded into the
        # ciphertext, since it -- not the ciphertext -- is the new keypad we
        # deliver locally in the clear below.
        rd = randDigits(25)
        newRandomKeyStr += rd
        tmp_s = stringSubtract(tmp_s, rd )
        newOtpTbl += tmp_s
        tmp_s = None

    keysA = None
    keysB = None

    # write out the combined table of keys in the clear to the combinedKeyFile.
    # this will be sent to the distant recipient, who will use it to decipher
    # the message.  The combinedKeyFile is not a key file, but a table of keys
    # that will be used to generate the new random key for the next message.

    writeFile( combinedKeyFile, codeGroups( newOtpTbl ) )

    newOtpTbl = None

    # write out the random table of keys (rd) in the clear to the prefix local
    for i in range( 25 ):
        tmpStr = newRandomKeyStr[ i*250 : (i+1)*250 ]
        filename = prefix + '{:02}'.format( i+1 ) + '.otk'
        writeFile(filename, codeGroups( tmpStr ) )

    if keepKeyFilesAfterUse:
        dbg( 'WARNING: Input key files being kept after use!' )
    else:
        wipeFile( file_i )
        wipeFile( file_j )
# end -- do_joinKeys()


# copied from above, though it is not the same - be careful

def do_unjoinKeys( file_i, file_j, combinedKeyFile, prefix ):
    if prefix is None:
        die( 'no key prefix given')

    ki = loadKeyPad( file_i )

    if( len(ki) != SHEETSIZE ):
        dbg( str(len(ki)) + ', ki:' + ki )
        die( 'bad first key:')

    kj = loadKeyPad( file_j )

    if( len(kj) != SHEETSIZE ):
        dbg( str(len(kj)) + ', kj:' + kj )
        die( 'bad second key')

    iTmp = combinateExpandedKeys( ki )
    jTmp = combinateExpandedKeys( kj )
    ki = None
    kj = None

    # append the two tables together, so we have 504 rows of 30 digits each
    # (25 + 5 checksum) -- must match do_joinKeys() exactly, or the two sides
    # build different checksum tables.
    tmp = [ iTmp[c] for c in range( len(iTmp) ) ] + [ jTmp[c] for c in range( len(jTmp) ) ]
    iTmp = None
    jTmp = None

    # create dictionary indexed by the unique checksum -- identical to
    # do_joinKeys()'s collision handling, so both sides land on the same
    # checksum -> row mapping.
    keys = {}
    x = ''

    checkSumString = '00000'
    csNum = 0

    for x in tmp:
        # take checksum / index added previously as row's last five digits
        checkSumString = x[-5:]

        # remove five digit checksum from string x
        x = x[:-5:]

        # find next empty spot by avoiding hash/checksum collisions -- must
        # match the wraparound in do_joinKeys() exactly, or the two sides
        # build different checksum orderings.
        while( checkSumString in keys ):
            csNum = ( int(checkSumString) + 1 ) % 100000
            checkSumString = '{:05}'.format(csNum )

        # dictionary of OTP keys indexed by checksum
        keys[checkSumString] = x

    tmp = None
    tmp = []

    sorted_keys = sorted(keys.keys())

    for k in sorted_keys:
        tmp.append( keys[k] )

    keys = None

    keys_size = len(tmp)
    half_keys_size = int( keys_size / 2 )

    # same interweaving split do_joinKeys() applies
    keysA = tmp[:half_keys_size]
    keysB = tmp[half_keys_size:]
    tmp = None

    # recompute K = keysA + keysB row by row, same as do_joinKeys()
    combinedKeys = ''
    for x in range(half_keys_size):
        combinedKeys += stringAdd( keysA[x], keysB[x] )

    keysA = None
    keysB = None

    # load the ciphertext the sender transmitted: ct = K - rd, per row, concatenated
    keyInput = loadKeyPad( combinedKeyFile )

    if len(keyInput) != len(combinedKeys):
        die( 'combined key file length does not match the expected keypad length.' )

    # invert do_joinKeys()'s masking step: ct = K - rd, so rd = K - ct
    newRandomKeyStr = stringSubtract( combinedKeys, keyInput )
    combinedKeys = None
    keyInput = None

    # write out the recovered random table of keys (rd) in the clear to the
    # prefix local -- must match do_joinKeys()'s slicing exactly.
    for i in range( 25 ):
        tmpStr = newRandomKeyStr[ i*250 : (i+1)*250 ]
        filename = prefix + '-' +  '{:02}'.format( i+1 ) + '.otk'
        writeFile(filename, codeGroups( tmpStr ) )

    if keepKeyFilesAfterUse:
        dbg( 'WARNING: Input key files being kept after use!' )
    else:
        wipeFile( file_i )
        wipeFile( file_j )
# do_unjoinKeys()



# ##########################################################################
'''
Return a table of every string "m" digits long, in range of "1..n"

This has no duplicates, so we see only "123", and not "132", "213", "231", "312", "321"

'''

def combo( m, n ):
    retVal = []
    tmp = ''
    c = combinations( list( range( 1, n+1 ) ), m )
    
    for r in c:
        tmp = ''
        for i in r:
            tmp += str(i)
        retVal.append(tmp)

    return retVal


'''
split message into "count" parts so it requires all those parts to read it.
Basically, everytime we split it, we generate a new random number, and subtract from the previous.
'''
def doMsgSplit( msg, count ):
    # return value is a table (dictionary)
    tbl = []
    msgLen = len(msg)
   
    r=''
    tmp=msg
    i=0
   
    for i in range(count-1):
        r = randDigits( msgLen )
        tbl.append( r )
        tmp = stringSubtract( tmp, r )
   
    tbl.append( tmp )
   
    return tbl


# split message into parts requiring minimum number to reconstruct
# write each member's messages for each group into files like: "zz995522xx--1-123.otp" with five letter groups.

# we also want a list of recipients so that we can generate mrssage files for each

def do_splitMsg( inputFilename, minparts, maxparts, prefix ):
    minparts = int( minparts )
    maxparts = int( maxparts )
    
    print( inputFilename, minparts, maxparts, prefix )
    
    fileText = readFile( inputFilename )
    
    if len(fileText) < 1:
        die( 'file: ' + inputFilename + ', contains no text.' )

    ''' We need to decide what happens when we send digits only text such as keys, or normal text that has to start with: a-z,~,#
    '''
    textToSplit = ''

    textToSplit = encode( fileText )

    clearText = stringDigits( textToSplit )
    tmp = combo( minparts, maxparts )
    
    '''
      couriers are named 0-9
      
      if we choose 3/9, we get 84 groups combos, or 28 copies of each message for each courier.
      
      Set Size    Portion     Combinations
      --------    -------     ------------
      3           2           3
      4           2           6
      5           2           10
      6           3           20
      7           3           35
      8           4           70
      9           4           126

      iterate through message groups and then
         iterate through members of message group
            create table entry indexed by courier name, and append each new record to it.
            
      end result is table where each record is all the parcels a courier needs to carry in one big blob.
    '''

    for messageGroup in tmp:      
        # print( messageGroup )
       
        # dms contains a table of the message split into parts for a single group
        dms = doMsgSplit( clearText, minparts )

        for i in range( len(messageGroup) ):
            fileName = prefix + messageGroup[i] + '-' + messageGroup +  '.otp'
            fileData = codeGroups( dms[ i ] ) + '\n'
            writeFile( fileName , fileData )

    if keepKeyFilesAfterUse:
        dbg( 'WARNING: Plain text being kept after use!' )
    else:
        wipeFile( inputFilename )



# merge messages split from above

def do_mergeMsg( outputFilename, filesTbl ):

    dbg( filesTbl )

    fileText = readFile( filesTbl[0] )
    fileLen = len( stringDigits( fileText ) )
    clearText = '0' * fileLen

    if len(clearText) < 1:
        die( 'file: ' + outputFilename + ', contains no text.')

    for i in filesTbl:
        filename = filesTbl[i]

        tmpText = stringDigits( readFile( filename ) )

        if len(tmpText) != len(clearText):
            die( 'message length of file: ' + filename + ' does not match.' )
        clearText = stringAdd( clearText, tmpText )

    plainText = decode( clearText )
    writeFile( outputFilename, plainText )

    wipeKeys( filesTbl )

'''Add two digit streams, optionally write result to the combined file and / or
optionally as a series of code groups to the key prefix.

Use for simplistically combining entropy of various key sources.

'''
def do_combineStreams( inputFile1, inputFile2, combinedFile, keyPrefix ):
    tmp1 = stringDigits( readFile( inputFile1 ) )
    tmp2 = stringDigits( readFile( inputFile2 ) )
    
    if len(tmp1) != len(tmp2):
        die( 'code stream digit counts are different' )
        
    tmp3 = stringAdd( tmp1, tmp2 )
    
    if combinedFile != '':
        writeFile( combinedFile, codeGroups( tmp3 ) )
    
    if keyPrefix != '':
        digitsNeeded = len(tmp3)
        
        pageCount = digitsNeeded // SHEETSIZE

        for x in range( 0, pageCount ):
            fn = keyPrefix + '-' + str( "{:02d}".format(x) ) + '.otk'
            s = tmp3[ (x*SHEETSIZE) :  ((x + 1) * SHEETSIZE) ]
            writeFile( fn, codeGroups( s ) )

    if keepKeyFilesAfterUse:
        dbg( 'WARNING: Input keys being kept after use!' )
    else:
        wipeFile( inputFile1 )
        wipeFile( inputFile2 )

        

# get a file's size in bytes, just blow up if there is an issue
def fileSize( fn ):
    retVal = -1
    file_stats = os.stat( fn )
    retVal = file_stats.st_size
    return retVal


# THIS MIGHT BE A "FEEL GOOD" FEATURE, as it is impossible to know what adversaries'
# current capabilties are, especially for smart media with wear levelling...

# we write all zeroes, all ones, all hex, then zeroes again.

def wipeFile( fn ):
    global testingMode
    global wipeRoundCount

    if fn == 'EDITOR':
        dbg( 'NOTE: wipeFile() skipped for EDITOR (no file on disk).' )
        return

    if testingMode:
        dbg('TESTING MODE: wipeFile( ' + fn + ') not executed.' )
        return

    try:
        # os.O_BINARY only exists on Windows; without it, os.write() there
        # runs through the CRT's text-mode translation, which rewrites any
        # 0x0A byte in our fill patterns into a 0x0D 0x0A pair on disk --
        # silently writing more bytes than intended. getattr(...) keeps
        # this a no-op on POSIX, where O_BINARY doesn't exist.
        fd = os.open( fn, os.O_RDWR | getattr( os, 'O_BINARY', 0 ) )
    except:
        die( '\nERROR: opening file: ' + str( fn )  )

    fs = fileSize( fn )

    for r in range( wipeRoundCount):
        # all ones
        # all zeroes
        # all hex
        # then zeros again
        
        for byteVal in [0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00]:
            # build the fill as raw bytes directly -- encoding a '\x80'-'\xff'
            # str through utf-8 would emit a 2-byte sequence per character,
            # writing 2x the file's length instead of a single repeated byte.
            s = bytes( [byteVal] ) * fs
            os.write( fd, s )

            # more feel good coding - could verify if does
            os.fsync( fd ) # this may be the requested one
            os.fsync( fd ) # this may be the one pending
            os.fsync( fd ) # the NOP that runs only once previous completes
            os.lseek( fd, 0, 0 )
    # TODO: rename, and create a bunch of empty files to clean the 
    # directory structure. 
    os.close( fd )
    os.remove( fn )

# given list of files, erase and wipe them.
def do_wipe( filesToErase ):    
    for f in filesToErase:
        wipeFile( filesToErase[f] )

# #############################################################################
# #############################################################################
# #############################################################################    

# save flags and values for command line arguments,
# with a sensible default for this unspecified
# return result as three parts: flags, args, and all the rest
def process_args():
    global useMorseShorts
    global RANDOM_DUPLICATES
    global keepKeyFilesAfterUse
    global wipeRoundCount
    global entropyGatheringSleepTime
    global testingMode
    
    tmp = 0
    
    # boolean flag args
    cmd_args = {        
        '-b':False, # combine code streams

        '-d':False, # decipher
        '-e':False, # encipher

        '-f':False, # fake message with generated key
        '-g':False, # generate key
        '-gz':False, # generate all-zero key (testing only, never for real use)
        
        '-k':False,  # keep key files after use (inverse of delete them) 

        '-j':False, # join/combine keys
        '-u':False, # unjoin/uncombine keys

        '-s':False, # split message
        '-m':False, # merge message

        '-v':False, # version
        '-h':False, # help
        '-hh':False, # help, verbose
        '-w':False, # wipe input and key files when done
        
        '-t':False, # testing mode, do not actually do anything other then test key length against encoded message
        
        '-z':False, # start using more shorts
    }
    # value args
    extra_args = {} # unnamed args, such as keys
    
    parm_args = {
        '-c':None,  # cipher text file
        '-i':None,  # input file
        '-a':None,  # another input file
        '-y':None,  # key file or prefix
        '-l':None,  # least/min parameter
        '-n':None,  # entropyGatheringSleepTime  - delay between random data block gathering
        '-o':None,  # output file
        '-p':None,  # plain text file
        '-q':None,  # RANDOM_DUPLICATES value
        '-r':None,  # wipe round count
        '-x':None,  # max value
        'extra':extra_args, # all others are placed here
    }
     
    args = {}
    argv = sys.argv
    argc = len(argv)
    
    if argc == 1:
        do_brief_help()
    
    x = 1

    while (x < argc):
        a = argv[x]
        
        if a == '-k':   # 
            dbg( 'ATTENTION!!! Keys must be deleted when used to maintain OPSEC!!!!' )
            keepKeyFilesAfterUse = True
        elif a == '-z':
            useMorseShorts = True
        elif a == "-t":
            testingMode = True
        elif a == '-q': # process ranom duplicates
            x+=1
            if x < argc:
                tmp = int(argv[x])
            else:
                die( 'Argument "' +  a + '" missing value.' )
            if tmp > 0:
                RANDOM_DUPLICATES = tmp
            else:
                die( "parameter for '-q' must be greater than zero." )
        elif a == '-r': # wipe random count
            x+=1
            if x < argc:
                tmp = int(argv[x])
            else:
                die( 'Argument "' +  a + '" missing value.' )
            if tmp > -1:
                wipeRoundCount = tmp
            else:
                die( "parameter for '-r' must not be negative." )
        elif a == '-n': # entropy gathering sleep time
            x+=1
            if x < argc:
                tmp = int(argv[x])
            else:
                die( 'Argument "' +  a + '" missing value.' )
            if tmp > -1:
                entropyGatheringSleepTime = tmp
            else:
                die( "parameter for '-n' must not be negative." )

        elif (a in cmd_args):
            cmd_args[a] = True
        elif (a in parm_args):
            x+=1
            if x < argc:
                parm_args[a] = argv[x]
            else:
                die( 'Argument "' +  a + '" missing value.' )
        else:
            extra_args[len(extra_args)] = a 
        x+=1

    return {0:cmd_args, 1:parm_args, 2:extra_args }


def otp_main():
    args = process_args()    
    init()

    cmd_args = args[0]
    commandFound = False

    for c in cmd_args:
        if cmd_args[c] == False:
            continue

        commandFound = True

        if c == '-v':
            do_version()
            
        elif c == '-h':
            do_brief_help()

        elif c == '-hh':
            do_help()
            
        elif c == '-d':
            do_decipher( args[1]['-i'], args[1]['-o'], args[2] )
          
        elif c == '-e':
            do_encipher( args[1]['-i'], args[1]['-o'], args[2] )
            
        elif c == '-f':
            do_fakeMsg( args[1]['-c'], args[1]['-p'], args[1]['-y'] )
            
        elif c == '-g':
            do_keygen( args[1]['-y'])

        elif c == '-gz':
            do_keygen( args[1]['-y'], zeroKeys=True )

        elif c == '-j':
            do_joinKeys( args[1]['-i'], args[1]['-a'], args[1]['-o'], args[1]['-y'] )
        elif c == '-u':
            do_unjoinKeys( args[1]['-i'], args[1]['-a'], args[1]['-c'], args[1]['-y'] )
            
        elif c == '-s':
            do_splitMsg( args[1]['-i'], args[1]['-l'], args[1]['-x'], args[2][0])
        elif c == '-m':
            do_mergeMsg( args[1]['-o'], args[2] )
            
        elif c == '-b':
            do_combineStreams( args[1]['-i'], args[1]['-a'], args[1]['-c'], args[1]['-y'] ) 
            
        elif c == '-w':
            do_wipe( args[2] )
        else:
            dbg( 'Unknown command option.' )
        # they all stop here - one command per run
        break

    if not commandFound:
        # -t/-k/-z/-q/-r/-n etc. are modifiers, not commands on their own --
        # process_args() applies them directly to globals and never marks
        # them True in cmd_args, so without a real command (-e, -d, -g, ...)
        # this loop would otherwise exit having silently done nothing.
        dbg( 'No command given -- nothing to do. Pass -h for usage.' )

def main():
    otp_main()
# main()
    
if __name__ == '__main__':
    main()
