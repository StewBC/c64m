"""Compact 6502 disassembler (documented opcodes; enough for demo RE)."""
# opcode table: byte -> (mnemonic, mode)
_M_IMP,_M_IMM,_M_ZP,_M_ZPX,_M_ZPY,_M_ABS,_M_ABX,_M_ABY,_M_IND,_M_IZX,_M_IZY,_M_REL,_M_ACC = range(13)
T={}
def d(op,mn,mode): T[op]=(mn,mode)
# load/store
for op,mn,m in [
 (0xA9,"LDA",_M_IMM),(0xA5,"LDA",_M_ZP),(0xB5,"LDA",_M_ZPX),(0xAD,"LDA",_M_ABS),(0xBD,"LDA",_M_ABX),(0xB9,"LDA",_M_ABY),(0xA1,"LDA",_M_IZX),(0xB1,"LDA",_M_IZY),
 (0xA2,"LDX",_M_IMM),(0xA6,"LDX",_M_ZP),(0xB6,"LDX",_M_ZPY),(0xAE,"LDX",_M_ABS),(0xBE,"LDX",_M_ABY),
 (0xA0,"LDY",_M_IMM),(0xA4,"LDY",_M_ZP),(0xB4,"LDY",_M_ZPX),(0xAC,"LDY",_M_ABS),(0xBC,"LDY",_M_ABX),
 (0x85,"STA",_M_ZP),(0x95,"STA",_M_ZPX),(0x8D,"STA",_M_ABS),(0x9D,"STA",_M_ABX),(0x99,"STA",_M_ABY),(0x81,"STA",_M_IZX),(0x91,"STA",_M_IZY),
 (0x86,"STX",_M_ZP),(0x96,"STX",_M_ZPY),(0x8E,"STX",_M_ABS),
 (0x84,"STY",_M_ZP),(0x94,"STY",_M_ZPX),(0x8C,"STY",_M_ABS),
 (0xAA,"TAX",_M_IMP),(0xA8,"TAY",_M_IMP),(0x8A,"TXA",_M_IMP),(0x98,"TYA",_M_IMP),(0xBA,"TSX",_M_IMP),(0x9A,"TXS",_M_IMP),
 (0x48,"PHA",_M_IMP),(0x68,"PLA",_M_IMP),(0x08,"PHP",_M_IMP),(0x28,"PLP",_M_IMP),
 (0x29,"AND",_M_IMM),(0x25,"AND",_M_ZP),(0x2D,"AND",_M_ABS),(0x3D,"AND",_M_ABX),(0x39,"AND",_M_ABY),(0x35,"AND",_M_ZPX),(0x21,"AND",_M_IZX),(0x31,"AND",_M_IZY),
 (0x09,"ORA",_M_IMM),(0x05,"ORA",_M_ZP),(0x0D,"ORA",_M_ABS),(0x1D,"ORA",_M_ABX),(0x19,"ORA",_M_ABY),(0x15,"ORA",_M_ZPX),(0x01,"ORA",_M_IZX),(0x11,"ORA",_M_IZY),
 (0x49,"EOR",_M_IMM),(0x45,"EOR",_M_ZP),(0x4D,"EOR",_M_ABS),(0x5D,"EOR",_M_ABX),(0x59,"EOR",_M_ABY),(0x55,"EOR",_M_ZPX),(0x41,"EOR",_M_IZX),(0x51,"EOR",_M_IZY),
 (0x69,"ADC",_M_IMM),(0x65,"ADC",_M_ZP),(0x6D,"ADC",_M_ABS),(0x7D,"ADC",_M_ABX),(0x79,"ADC",_M_ABY),(0x75,"ADC",_M_ZPX),(0x61,"ADC",_M_IZX),(0x71,"ADC",_M_IZY),
 (0xE9,"SBC",_M_IMM),(0xE5,"SBC",_M_ZP),(0xED,"SBC",_M_ABS),(0xFD,"SBC",_M_ABX),(0xF9,"SBC",_M_ABY),(0xF5,"SBC",_M_ZPX),(0xE1,"SBC",_M_IZX),(0xF1,"SBC",_M_IZY),
 (0xC9,"CMP",_M_IMM),(0xC5,"CMP",_M_ZP),(0xCD,"CMP",_M_ABS),(0xDD,"CMP",_M_ABX),(0xD9,"CMP",_M_ABY),(0xD5,"CMP",_M_ZPX),(0xC1,"CMP",_M_IZX),(0xD1,"CMP",_M_IZY),
 (0xE0,"CPX",_M_IMM),(0xE4,"CPX",_M_ZP),(0xEC,"CPX",_M_ABS),
 (0xC0,"CPY",_M_IMM),(0xC4,"CPY",_M_ZP),(0xCC,"CPY",_M_ABS),
 (0x24,"BIT",_M_ZP),(0x2C,"BIT",_M_ABS),
 (0xE6,"INC",_M_ZP),(0xF6,"INC",_M_ZPX),(0xEE,"INC",_M_ABS),(0xFE,"INC",_M_ABX),
 (0xC6,"DEC",_M_ZP),(0xD6,"DEC",_M_ZPX),(0xCE,"DEC",_M_ABS),(0xDE,"DEC",_M_ABX),
 (0xE8,"INX",_M_IMP),(0xC8,"INY",_M_IMP),(0xCA,"DEX",_M_IMP),(0x88,"DEY",_M_IMP),
 (0x0A,"ASL",_M_ACC),(0x06,"ASL",_M_ZP),(0x16,"ASL",_M_ZPX),(0x0E,"ASL",_M_ABS),(0x1E,"ASL",_M_ABX),
 (0x4A,"LSR",_M_ACC),(0x46,"LSR",_M_ZP),(0x56,"LSR",_M_ZPX),(0x4E,"LSR",_M_ABS),(0x5E,"LSR",_M_ABX),
 (0x2A,"ROL",_M_ACC),(0x26,"ROL",_M_ZP),(0x36,"ROL",_M_ZPX),(0x2E,"ROL",_M_ABS),(0x3E,"ROL",_M_ABX),
 (0x6A,"ROR",_M_ACC),(0x66,"ROR",_M_ZP),(0x76,"ROR",_M_ZPX),(0x6E,"ROR",_M_ABS),(0x7E,"ROR",_M_ABX),
 (0x18,"CLC",_M_IMP),(0x38,"SEC",_M_IMP),(0x58,"CLI",_M_IMP),(0x78,"SEI",_M_IMP),(0xD8,"CLD",_M_IMP),(0xF8,"SED",_M_IMP),(0xB8,"CLV",_M_IMP),
 (0x4C,"JMP",_M_ABS),(0x6C,"JMP",_M_IND),(0x20,"JSR",_M_ABS),(0x60,"RTS",_M_IMP),(0x40,"RTI",_M_IMP),(0x00,"BRK",_M_IMP),(0xEA,"NOP",_M_IMP),
 (0x10,"BPL",_M_REL),(0x30,"BMI",_M_REL),(0x50,"BVC",_M_REL),(0x70,"BVS",_M_REL),(0x90,"BCC",_M_REL),(0xB0,"BCS",_M_REL),(0xD0,"BNE",_M_REL),(0xF0,"BEQ",_M_REL),
]:
    d(op,mn,m)

def disasm(mem, start, end):
    out=[]; pc=start
    while pc<end:
        op=mem[pc]
        if op not in T:
            out.append((pc,"%02X"%op,".byte $%02X"%op)); pc+=1; continue
        mn,mode=T[op]
        b=mem
        if mode in (_M_IMP,_M_ACC):
            txt=mn+("" if mode==_M_IMP else " A"); raw="%02X"%op; ln=1
        elif mode==_M_IMM:
            txt="%s #$%02X"%(mn,b[pc+1]); raw="%02X %02X"%(op,b[pc+1]); ln=2
        elif mode==_M_ZP:
            txt="%s $%02X"%(mn,b[pc+1]); raw="%02X %02X"%(op,b[pc+1]); ln=2
        elif mode==_M_ZPX:
            txt="%s $%02X,X"%(mn,b[pc+1]); raw="%02X %02X"%(op,b[pc+1]); ln=2
        elif mode==_M_ZPY:
            txt="%s $%02X,Y"%(mn,b[pc+1]); raw="%02X %02X"%(op,b[pc+1]); ln=2
        elif mode==_M_IZX:
            txt="%s ($%02X,X)"%(mn,b[pc+1]); raw="%02X %02X"%(op,b[pc+1]); ln=2
        elif mode==_M_IZY:
            txt="%s ($%02X),Y"%(mn,b[pc+1]); raw="%02X %02X"%(op,b[pc+1]); ln=2
        elif mode==_M_REL:
            off=b[pc+1]; tgt=pc+2+(off-256 if off>=128 else off)
            txt="%s $%04X"%(mn,tgt); raw="%02X %02X"%(op,b[pc+1]); ln=2
        else: # 3-byte abs modes
            addr=b[pc+1]|(b[pc+2]<<8)
            suf={_M_ABS:"",_M_ABX:",X",_M_ABY:",Y",_M_IND:""}[mode]
            if mode==_M_IND: txt="%s ($%04X)"%(mn,addr)
            else: txt="%s $%04X%s"%(mn,addr,suf)
            raw="%02X %02X %02X"%(op,b[pc+1],b[pc+2]); ln=3
        out.append((pc,raw,txt)); pc+=ln
    return out

def show(mem,start,end):
    for pc,raw,txt in disasm(mem,start,end):
        print("  $%04X: %-9s %s"%(pc,raw,txt))
