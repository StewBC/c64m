Suggestions to consider:

Please review the keyboard mapping code recently added to `main.c`.

1. Re-check whether this belongs in `main.c`.

   * Per `MASTER.md`, `main.c` should mostly wire startup/shutdown and top-level loop behavior.
   * SDL event handling and host input translation seem more appropriate in `frontend/` or `platform/`.
   * The machine should own the C64 keyboard matrix, but it should not know about SDL keycodes.

2. Avoid sending SDL keycodes or SDL events into runtime/machine.

   * Convert host input on the UI/frontend side into emulator-level input commands.
   * Runtime should receive project-defined commands/events, not SDL concepts.

3. Check shifted punctuation handling.

   * `Shift+;` likely arrives as `SDLK_SEMICOLON` plus `KMOD_SHIFT`, not as `SDLK_COLON`.
   * If the mapper only switches on `SDLK_COLON`, colon may never be generated.
   * Be careful not to both map `Shift+;` to colon and also inject C64 shift for the same logical keypress.

4. Consider separating two concepts:

   * physical C64 key matrix presses
   * host text input / printable character intent

5. Add focused tests or manual checks for:

   * `;`
   * `:`
   * `=`
   * `+`
   * `-`
   * `*`
   * `/`
   * shifted number-row punctuation if supported
   * left/right shift behavior

6. User measured current mapping - the User pressed these keys and notes what happend in the emulator.  Observed measurements match  almost a C64 layout.  This should be an altrenative "set" which matches the C64 keyboard physical layont.  The default, however, should be the SDL keys resulting in the equivalent keys in the emulator, ie " on use keyboard generates a " in the C64 in the emulator as well.

This is a table that outlines what the user observed.  Key = macOS kbd key.

Key         Generated            Expected       Shift + Key       Generated      Expected
=========== ==================== ============== ================= ============== ==============
`           <nothing>            <-             ~                 <nothing>      <nothing>
1           1                    1              !                 !              !
2           2                    2              @                 "              @
3           3                    3              #                 #              #
4           4                    4              $                 $              $
5           5                    5              %                 %              %
6           6                    6              &                 &              <up arrow>
7           7                    7              '                 '              &
8           8                    8              (                 (              *
9           9                    9              )                 )              (
0           0                    0              0                 0              )
-           -                    0              0                 |              <?>
=           =                    0              0                 =              +
<backspace> <backspace>          <backspace>    <insert>          <insert>       <insert>
INS         <nothing>            <nothing>      INS               <nothing>      <nothing>
HOME        HOME                 HOME           CLR/HOME          CLR/HOME.      CLR/HOME
PG/UP       <nothing>            <nothing>      PG/UP             <nothing>      <nothing>
TAB         <nothing>            <nothing>      TAB               <nothing>      <nothing>
q           Q                    Q              graphic chars     graphic chars  graphic chars
w           W                    W              graphic chars     graphic chars  graphic chars
e           E                    E              graphic chars     graphic chars  graphic chars
r           R                    R              graphic chars     graphic chars  graphic chars
t           T                    T              graphic chars     graphic chars  graphic chars
y           Y                    Y              graphic chars     graphic chars  graphic chars
u           U                    U              graphic chars     graphic chars  graphic chars
i           I                    I              graphic chars     graphic chars  graphic chars
o           O                    O              graphic chars     graphic chars  graphic chars
p           P                    P              graphic chars     graphic chars  graphic chars
[           <nothing>            <nothing>      {                 <nothing>      <nothing> #1
]           <nothing>            <nothing>      }                 <nothing>      <nothing> #2
\           <nothing>            <nothing>      |                 <nothing>      <nothing> #3
DEL         RESTORE              RESTORE        DEL               <nothing>      <nothing>
END         <nothing>            <nothing>      END               <nothing>      <nothing>
PG/DN       <nothing>            <nothing>      PG/DN             <nothing>      <nothing>
CAPS        <nothing>            <nothing>                        <nothing>      <nothing>
a           A                    A              graphic chars     graphic chars  graphic chars
s           S                    S              graphic chars     graphic chars  graphic chars
d           D                    D              graphic chars     graphic chars  graphic chars
f           F                    F              graphic chars     graphic chars  graphic chars
g           G                    G              graphic chars     graphic chars  graphic chars
h           H                    H              graphic chars     graphic chars  graphic chars
j           J                    J              graphic chars     graphic chars  graphic chars
k           K                    K              graphic chars     graphic chars  graphic chars
l           L                    L              graphic chars     graphic chars  graphic chars
;           ;                    ;              :                 ]              :
'           <nothing>            '              "                 <nothing>      "
ENTER       ENTER                ENTER          <not sure> #4
LSHIFT      SHIFT                SHIFT          N/A               N/A            N/A
z           Z                    Z              graphic chars     graphic chars  graphic chars
x           X                    X              graphic chars     graphic chars  graphic chars
c           C                    C              graphic chars     graphic chars  graphic chars
v           V                    V              graphic chars     graphic chars  graphic chars
b           B                    B              graphic chars     graphic chars  graphic chars
n           N                    N              graphic chars     graphic chars  graphic chars
m           M                    M              graphic chars     graphic chars  graphic chars
,           ,                    ,              <                 <              <
.           .                    .              >                 >              >
/           /                    /              ?                 ?              ?
RSHIFT      SHIFT                SHIFT          N/A               N/A            N/A
CTRL #5
OPTION
COMMAND
SPACE       SPACE                SPACE          SPACE             petscii-160    petscii-160
CRSR-UP     CRSR-UP              CRSR-UP        CRSR-UP           CRSR-UP        CRSR-UP
CRSR-RIGHT  CRSR-RIGHT           CRSR-RIGHT     CRSR-RIGHT        CRSR-LEFT      CRSR-RIGHT
CRSR-DOWN   CRSR-DOWN            CRSR-DOWN      CRSR-DOWN         CRSR-UP        CRSR-DOWN 
CRSR-LEFT   CRSR-LEFT            CRSR-LEFT      CRSR-LEFT         CRSR-LEFT      CRSR-LEFT 

#1 user suggests @
#2 user suggests *
#3 user suggests <up arrow>.  How was the pi character generated?  \ and | should map to <up arrow> and the Pi character in a way that makes sense
#4 Generated CR/LF visually - crsr down and back to margin but no syntax error where a normal ENTER would have given SYNTAX ERROR.  User not sure what a real C64 does.
#5 The user forgot that the C64 had a CONTROL key.  The user asked for run, step etc to be mapped through control+<key> combos.  That was also a mistake.  These should be OPTION+<key> combos.  CONTROL should serve the same function it did on the C64.  

Keep in mind that the user makes mistakes.  If something in the above table looks suspicious, stop and get verification.

7. Keep the architecture boundary intact:

   * SDL stays in platform/frontend.
   * Runtime owns the live machine.
   * Machine owns the keyboard matrix.
   * Communication crosses threads through runtime_client-style project commands, not live pointers or SDL objects.
