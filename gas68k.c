#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include "dict.h"

#define LAB_LEN	32
#define BASE	0x1c
FILE *fin;
FILE *fout; 
char buf[4096];
Dict labels;
int dump_labels = 0;
int line_no = 0;
int last_op_addr;
enum SIZE { BYTE, WORD, LONG, LONG_FIXUP };

/* to convert weird move sizes to standard sizes */
const int move_size[4] = { 1, 3, 2, 0 };
const char *Bcc_str[16] = {
	"ra",NULL,"hi","ls","cc","cs","ne","eq",
	"vc","vs","pl","mi","ge","lt","gt","le"
};
const char *DBcc_str[16] = {
	"t","f","hi","ls","cc","cs","ne","eq",
	"vc","vs","pl","mi","ge","lt","gt","le"
};

#define check(ch)	do {  \
	if (*pos != (ch)) error ("Expected '%c'", ch); \
	pos++; \
	} while (0);
#define check_whitespace()	do { \
	if (!isspace (*pos)) error ("Line malformed. (%s)", pos); \
	while (isspace (*pos)) pos++; \
	} while (0);

#define get_bitpos()	((int)ftell (fout))
#define set_bitpos(pos)	(fseek (fout,pos,SEEK_SET))

enum REG_MODES {
	MODE_DREG,
	MODE_AREG,
	MODE_IND,
	MODE_POST,
	MODE_PRE,
	MODE_OFFSET,
	MODE_INDEX,
	MODE_EXT
};

enum EXT_MODES {
	EXT_MEM_W,
	EXT_MEM_L,
	EXT_PC_OFFSET,
	EXT_PC_INDEX,
	EXT_IMM
};

#define F_DREG		(1<<0)
#define F_AREG		(1<<1)
#define F_IND		(1<<2)
#define F_POST		(1<<3)
#define F_PRE		(1<<4)
#define F_OFFSET	(1<<5)
#define F_INDEX		(1<<6)
#define F_MEM_W		(1<<7)
#define F_MEM_L		(1<<8)
#define F_IMM		(1<<9)
#define F_PC_OFFSET	(1<<10)
#define F_PC_INDEX	(1<<11)

#define F_NOLABELS	(1<<12)

#define F_NOIMM_NOPC_NOAREG	(F_DREG | F_IND | F_POST | F_PRE | \
		F_OFFSET | F_INDEX | F_MEM_W | F_MEM_L)
#define F_NOIMM_NOPC	(F_DREG | F_AREG | F_IND | F_POST | F_PRE | \
		F_OFFSET | F_INDEX | F_MEM_W | F_MEM_L)
#define F_ALL (0xffffffff & (~(F_NOLABELS)))
#define F_ALL_NOAREG	(F_ALL & (~F_AREG))

typedef union Opcode {
	unsigned short code;
	struct move {
		uint src_reg : 3;
		uint src_mode : 3;
		uint dest_mode : 3;
		uint dest_reg : 3;
		uint size : 2;
		uint op : 2;
	} move;
	struct adr_index {
		int displacement : 8;
		uint zeros : 3;
		uint ind_size : 1;
		uint reg : 3;
		uint reg_type : 1;
	} adr_index;
	struct addq {
		uint dest_reg : 3;
		uint dest_mode : 3;
		uint size : 2;
		uint issub : 1;
		uint data : 3;
		uint op : 4;
	} addq;
	struct jmp {
		uint dest_reg : 3;
		uint dest_mode : 3;
		uint op : 10;
	} jmp;
	struct addi {
		uint dest_reg : 3;
		uint dest_mode : 3;
		uint size : 2;
		uint OP6 : 8;
	} addi;
	/* size and effective address */
	struct type1 {
		uint ea_reg : 3;
		uint ea_mode : 3;
		uint size : 2;
		uint op : 8;
	} type1;
	/* reg, opmode, ea */
	struct type2 {
		uint ea_reg : 3;
		uint ea_mode : 3;
		uint op_mode : 3;
		uint reg : 3;
		uint op : 4;
	} type2;
	struct MemShift {
		uint ea_reg : 3;
		uint ea_mode : 3;
		uint OP3 : 2;
		uint dr : 1;
		uint type : 2;
		uint OP0x1c : 5;
	} MemShift;
	struct ASx {
		uint reg : 3;
		uint OP0 : 2;
		uint ir : 1;
		uint size : 2;
		uint dr : 1;
		uint count_reg : 3;
		uint OP0xe : 4;
	} ASx;
	struct abcd {
		uint src_reg : 3;
		uint rm : 1;
		uint OP16 : 5;
		uint dest_reg : 3;
		uint OP12 : 4;
	} abcd;
	struct addx {
		uint src_reg : 3;
		uint rm : 1;
		uint OP0 : 2;
		uint size : 2;
		uint OP1 : 1;
		uint dest_reg : 3;
		uint op : 4;
	} addx;
	struct cmpm {
		uint src_reg : 3;
		uint OP1_1 : 3;
		uint size : 2;
		uint OP2_1 : 1;
		uint dest_reg : 3;
		uint OP0xb : 4;
	} cmpm;
	/* branches */
	struct DBcc {
		uint reg : 3;
		uint OP25 : 5;
		uint cond : 4;
		uint OP5 : 4;
	} DBcc;
	struct Bcc {
		int displacement : 8;
		uint cond : 4;
		uint op : 4;
	} Bcc;
	struct bra {
		int displacement : 8;
		uint op : 8;
	} bra;
	struct movem {
		uint dest_reg : 3;
		uint dest_mode : 3;
		uint sz : 1;
		uint ONE : 3;
		uint dr : 1;
		uint NINE : 5;
	} movem;
	struct lea {
		uint dest_reg : 3;
		uint dest_mode : 3;
		uint SEVEN : 3;
		uint reg : 3;
		uint FOUR : 4;
	} lea;
	struct moveq {
		int data : 8;
		uint ZERO : 1;
		uint reg : 3;
		uint SEVEN : 4;
	} moveq;
	struct exg {
		uint dest_reg : 3;
		uint op_mode : 5;
		uint OP1 : 1;
		uint src_reg : 3;
		uint OP0xc : 4;
	} exg;
} Opcode;

struct Fixup {
	int rel_to;
	int line_no;
	int size;
	int adr;
	char label[LAB_LEN];
	/* used for gas_offset (label+int) */
	int offset;

	struct Fixup *next;
};

struct Label {
	const char *name;
	int val;
	enum LAB_TYPE {
		L_ADDR,
		L_CONST
	} type;
};

struct ImmVal {
	int has_label;
	char label[LAB_LEN];
	int val;
	/* gas has stuff like 'lea label+10,a0', so we need this: */
	int gas_offset;
};

struct Fixup *fix_first = NULL;
struct Fixup *fix_last = NULL;
void add_fixup (int adr, int size, const char *label, int offset)
{
	struct Fixup *fix = malloc (sizeof (struct Fixup));
	fix->adr = adr;
	fix->size = size;
	fix->next = NULL;
	fix->rel_to = (last_op_addr + 2) - BASE;
	fix->line_no = line_no;
	fix->offset = offset;
	strcpy (fix->label, label);
	if (fix_first == NULL)  fix_first = fix;
	if (fix_last == NULL) fix_last = fix;
	else {
		fix_last->next = fix;
		fix_last = fix;
	}
}

void error (const char *format, ...)
{
	va_list argptr;

	printf ("Error at line %d: ", line_no);
	va_start (argptr, format);
	vprintf (format, argptr);
	va_end (argptr);
	printf ("\n");
	
	fclose (fout);
	remove ("aout.prg");
	exit (EXIT_FAILURE);
}
	
void add_label (const char *name, int type, int val)
{
	struct Label *lab = malloc (sizeof (struct Label));
	if (dict_get (&labels, name)) error ("Label %s redefined.", name);
	lab->val = val;
	lab->type = type;
	dict_set (&labels, name, lab);
}

struct Label *get_label (const char *name)
{
	struct Label *lab;
	struct Node *n = dict_get (&labels, name);
	
	if (n == NULL) return NULL;
	lab = n->obj;
	lab->name = n->key;
	return lab;
}

void wr_byte(unsigned char x)
{
	fputc (x, fout);
}

void wr_short(short x)
{
	wr_byte((unsigned char)((x>>8) & 0xff));
	wr_byte((unsigned char)(x & 0xff));
}

void wr_int(int x)
{
	wr_byte((unsigned char)((x>>24) & 0xff));
	wr_byte((unsigned char)((x>>16) & 0xff));
	wr_byte((unsigned char)((x>>8) & 0xff));
	wr_byte((unsigned char)(x & 0xff));
}


static inline int get_size (int ch)
{
	switch (ch) {
		case 'b': return BYTE;
		case 'w': return WORD;
		case 'l': return LONG;
		default: error ("Invalid size '.%c'.", ch); return 0;
	}
}

#define IS_START_OF_LABEL(chr)	(isalpha (chr) || ((chr) == '.') || ((chr) == '_'))
#define IS_VALID_LABEL_CHR(chr)	(isalnum (chr) || ((chr) == '.') || ((chr) == '_'))
		
char *rd_label (char *buf, char *lab_buf)
{
	char snipped;
	int len;
	char *pos = buf;
	
	if (!IS_START_OF_LABEL (*buf)) error ("Label expected.");
	/* label */
	do {
		pos++;
	} while (IS_VALID_LABEL_CHR (*pos));
	/* snip */
	snipped = *pos;
	*pos = '\0';

	len = strlen (buf);
	if (len > LAB_LEN-1) error ("Label too long.");
	strncpy (lab_buf, buf, LAB_LEN);

	/* weird thing gas does. cmp.l label.l,%d2 */
	if ((len >= 3) && (lab_buf[len-2] == '.')) {
		if (lab_buf[len-1] == 'l') lab_buf[len-2] = '\0';
	}
	
	lab_buf[LAB_LEN-1] = '\0';
	*pos = snipped;
	return pos;
}


void check_range (int *val, enum SIZE size)
{
	switch (size) {
		case BYTE: if ((*val < -128) || (*val > 255)) goto err;
			if (*val > 127) *val -= 256;
			break;
		case WORD: if ((*val < -32768) || (*val > 65535)) goto err;
			if (*val > 32767) *val -= 65536;
			break;
		default: break;
	}
	return;
err:
	error ("Data too large.");
}

/* hex or dec, 0xdeadbeef or $deadbeef format */
char *read_number (char *pos, int *val)
{
	if (*pos == '$') {
		/* hex value */
		pos++;
		if (sscanf (&pos[0], "%x", val) != 1) goto err;
		while (isxdigit (*pos)) pos++;
	} else if ((pos[0] == '0') && (pos[1] == 'x')) {
		/* hex value */
		pos+=2;
		if (sscanf (&pos[0], "%x", val) != 1) goto err;
		while (isxdigit (*pos)) pos++;
	} else if (isdigit (*pos) || (*pos == '-')) {
		/* dec value */
		if (sscanf (&pos[0], "%d", val) != 1) goto err;
		if (*pos == '-') pos++;
		while (isdigit (*pos)) pos++;
	} else {
err:
		error ("Malformed immediate value.");
		return pos;
	}
	return pos;
}

char *get_imm (char *pos, struct ImmVal *imm, int flags)
{
	struct Label *lab;
	
	imm->gas_offset = 0;

	imm->has_label = 0;
	while (isspace (*pos)) pos++;
	if (flags & F_IMM) {
		if (*pos != '#') goto err;
		pos++;
	}
	if (IS_START_OF_LABEL (*pos)) {
		/* label */
		pos = rd_label (pos, imm->label);
		lab = get_label (imm->label);
		imm->val = 0;
		if (lab) {
			if (lab->type == L_CONST) imm->val = lab->val;
			else imm->has_label = 1;

		} else {
			imm->has_label = 1;
		}
		
		/* gas label+integer */
		if (*pos == '+') {
			pos++;
			pos = read_number (pos, &imm->gas_offset);
		} else if (*pos == '-') {
			pos++;
			pos = read_number (pos, &imm->gas_offset);
			imm->gas_offset = -imm->gas_offset;
		}
		
		if ((flags & F_NOLABELS) && (imm->has_label)) {
			error ("Label not allowed.");
		}
	} else {
		pos = read_number (pos, &imm->val);
	}
	return pos;
err:
	error ("Malformed immediate value.");
	return NULL;
}

int is_end (const char *buf)
{
	while (isspace (*buf)) buf++;
	/* end of line comments */
	if (*buf == '*') return 1;
	if (*buf == ';') return 1;
	if (*buf) return 0;
	return 1;
}

void check_end (const char *buf)
{
	if (!is_end (buf)) error ("Garbage at end of line.");
}

typedef struct ea_t {
	int mode;
	int reg;
	int op_size;
	struct ImmVal imm;
	union Ext {
		short ext;
		struct {
			int displacement : 8;
			int ZERO : 3;
			uint size : 1;
			uint reg : 3;
			uint d_or_a : 1;
		} _;
	} ext;
} ea_t;

void wr_ea (ea_t *ea)
{
	if (ea->mode == 5) {
		if (ea->imm.has_label) error ("Relative not allowed.");
		wr_short (ea->imm.val);
		return;
	}
	if (ea->mode == 6) {
		if (ea->imm.has_label) error ("Relative not allowed.");
		wr_short (ea->ext.ext);
		return;
	}
	if (ea->mode == 7) {
		/* $xxx.w */
		if (ea->reg == 0) {
			wr_short (ea->imm.val);
			return;
		}
		/* $xxx.l */
		else if (ea->reg == 1) {
			if (ea->imm.has_label) goto abs_lab32;
			wr_int (ea->imm.val);
			return;
		}
		/* immediate */
		else if (ea->reg == 4) {
			if (ea->imm.has_label) {
				if (ea->op_size != LONG)
					error ("Relative not allowed.");
				goto abs_lab32;
			}
			if (ea->op_size == LONG) {
				wr_int (ea->imm.val);
			} else if (ea->op_size == WORD) {
				wr_short (ea->imm.val);
			} else {
				wr_short (ea->imm.val & 0xff);
			}
			return;
		}
		/* PC + offset */
		else if (ea->reg == 2) {
			if (ea->imm.has_label) goto rel_lab16;
			error ("Absolute value not allowed.");
			return;
		}
		/* PC + INDEX + OFFSET */
		else if (ea->reg == 3) {
			if (!ea->imm.has_label) error ("Absolute value not allowed.");
			wr_short (ea->ext.ext);
			add_fixup (get_bitpos()-1, BYTE, ea->imm.label, ea->imm.gas_offset);
			return;
		}
	}
	return;
rel_lab16:
	/* resolve a relative 16-bit label (offset) */
	wr_short (0);
	add_fixup (get_bitpos()-2, WORD, ea->imm.label, ea->imm.gas_offset);
	return;
abs_lab32:
	/* resolve an absolute 32-bit label */
	wr_int (0);
	add_fixup (get_bitpos ()-4, LONG_FIXUP, ea->imm.label, ea->imm.gas_offset);
}

char *get_reg (char *pos, int *reg_num, int flags)
{
	int type;
	while (isspace (*pos)) pos++;
	check ('%');
	if ((flags & F_AREG) && (strncmp (pos, "sp", 2) == 0)) {
		pos += 2;
		*reg_num = 8+7;
		return pos;
	}
	if ((flags & F_AREG) && (strncmp (pos, "fp", 2) == 0)) {
		pos += 2;
		*reg_num = 8+6;
		return pos;
	}
	if ((flags & F_AREG) && (pos[0] == 'a')) type = 8;
	else if ((flags & F_DREG) && (pos[0] == 'd')) type = 0;
	else goto err;
	pos++;
	if (sscanf (&pos[0], "%d", reg_num) != 1) goto err;
	
	*reg_num += type;
	while (isdigit (*pos)) pos++;
	return pos;
err:
	error ("Expected a register.");
	return NULL;
}
	
void ea_error (int flags)
{
	error ("Malformed effective address. Valid modes are: %s%s%s%s%s%s%s%s%s%s%s%s",
			(flags & F_DREG ? "dreg " : ""),
			(flags & F_AREG ? "areg " : ""),
			(flags & F_IND ? "indirect " : ""),
			(flags & F_POST ? "postincrement ": ""),
			(flags & F_PRE ? "predecrement ": ""),
			(flags & F_OFFSET ? "areg-offset ": ""),
			(flags & F_OFFSET ? "areg-offset-index ": ""),
			(flags & F_MEM_W ? "absolute.w ": ""),
			(flags & F_MEM_L ? "absolute.l ": ""),
			(flags & F_IMM ? "immediate ": ""),
			(flags & F_PC_OFFSET ? "pc-relative ": ""),
			(flags & F_PC_INDEX ? "pc-rel-index ": ""));
}
	
void check_ea (ea_t *ea, int op_size, int flags)
{
	if ((ea->mode < MODE_EXT) && 
		(!(flags & (1<<ea->mode)))) ea_error (flags);
	
	switch (ea->reg) {
		case EXT_MEM_W:
			if (flags & F_MEM_W) ea_error (flags);
			break;
		case EXT_MEM_L:
			if (flags & F_MEM_L) ea_error (flags);
			break;
		case EXT_IMM:
			if (flags & F_IMM) ea_error (flags);
			break;
		case EXT_PC_OFFSET:
			if (flags & F_PC_OFFSET) ea_error (flags);
			break;
		case EXT_PC_INDEX:
			if (flags & F_PC_INDEX) ea_error (flags);
			break;
	}
}

char *get_ea (char *pos, ea_t *ea, int op_size, int flags)
{
	int reg;
	int size;
	char *orig;
	ea->imm.has_label = 0;	
	ea->op_size = op_size;
	while (isspace (*pos)) pos++;
	orig = pos;
	
	
	if ((pos[0] == '%') && (pos[1] == 'd') && isdigit (pos[2])) {
		pos+=2;
		if (sscanf (&pos[0], "%d", &ea->reg) != 1) goto poopdog;
		ea->mode = 0;
		if ((ea->reg < 0) || (ea->reg > 7)) goto poopdog;
		while (isdigit (*pos)) pos++;
		if (!(flags & F_DREG)) goto err;
		return pos;
	}
	if ((pos[0] == '%') && (pos[1] == 'a') && isdigit (pos[2])) {
		pos+=2;
		if (sscanf (&pos[0], "%d", &ea->reg) != 1) goto poopdog;
		ea->mode = 1;
		if ((ea->reg < 0) || (ea->reg > 7)) goto poopdog;
		while (isdigit (*pos)) pos++;
		if (!(flags & F_AREG)) goto err;
		return pos;
	}
	if (strncmp (pos, "%sp", 3) == 0) {
		pos += 3;
		ea->mode = 1;
		ea->reg = 7;
		return pos;
	}
	if (strncmp (pos, "%fp", 3) == 0) {
		pos += 3;
		ea->mode = 1;
		ea->reg = 6;
		return pos;
	}
poopdog:
	pos = orig;
	if ((*pos == '#') && (flags & F_IMM)) {
		pos = get_imm (pos, &ea->imm, F_IMM);
		check_range (&ea->imm.val, op_size);
		ea->mode = 7;
		ea->reg = 4;
		return pos;
	}
	if ((flags & F_PRE) && (pos[0] == '-') && (pos[1] == '(')) {
		pos += 2;
		pos = get_reg (pos, &ea->reg, F_AREG);
		check (')');
		ea->mode = 0x4;
		return pos;
	}
	if ((flags & (F_IND | F_POST | F_OFFSET | F_INDEX)) && (pos[0] == '(')) {
		check ('(');
		if ((flags & F_OFFSET) && ((pos[0] == '-') || (isdigit (pos[0])))) {
			/* some weird gas syntax. (-10,%a6) */
			ea->mode = 5;
			pos = get_imm (pos, &ea->imm, F_NOLABELS);
			check (',');
			pos = get_reg (pos, &ea->reg, F_AREG);
			check (')');
			return pos;
		}
		/* gas loves to do 'pea (label)' where 'pea label' would do */
		if (IS_START_OF_LABEL (*pos)) {
			pos = get_imm (pos, &ea->imm, 0);
			
			while (isspace (*pos)) pos++;
			if (*pos == ')') pos++;
			if (((*pos == ',') || (is_end (pos))) && (flags & F_MEM_L)) {
				/* Immed memory with label */
				ea->mode = 0x7;
				ea->reg = 1;
				return pos;
			} else {
				goto err;
			}
		} else {
			/* read register in '(Regx)' */
			pos = get_reg (pos, &ea->reg, F_AREG);
		}
		if ((*pos == ',') && (F_INDEX)) {
			/* another gas thing. (%a1,%d0.l) with no offset given */
			ea->imm.val = 0;
			goto f_index;
		}
		check (')');
		if ((flags & F_POST) && (pos[0] == '+')) {
			ea->mode = 3;
			return ++pos;
		} else if (flags & F_IND) {
			ea->mode = 2;
			return pos;
		} else goto err;
	}
	if ((*pos == '$') || isdigit (*pos) || (*pos == '-') || IS_START_OF_LABEL (*pos)) {
		if (IS_START_OF_LABEL (*pos)) {
			pos = get_imm (pos, &ea->imm, 0);
		} else if (*pos == '$') {
			pos++;
			if (sscanf (pos, "%x", &ea->imm.val) != 1) goto err;
			while (isxdigit (*pos)) pos++;
		} else {
			if (sscanf (pos, "%d", &ea->imm.val) != 1) goto err;
			if (*pos == '-') pos++;
			while (isdigit (*pos)) pos++;
		}
		/* Immediate memory (not label) */
		if ((*pos == '.') && (flags & (F_MEM_W | F_MEM_L))) {
			pos++;
			size = get_size (*pos);
			pos++;
			ea->mode = 0x7;
			if (size == WORD) {
				ea->reg = 0;
				if ((ea->imm.val < 0) || (ea->imm.val > 65535)) error ("Immediate value too large.");
			}
			else if (size == LONG) ea->reg = 1;
			else error ("Bad size.");
			return pos;
		}
		while (isspace (*pos)) pos++;
		if (((*pos == ',') || (is_end (pos))) && (flags & F_MEM_L)) {
			/* Immed memory with label */
			ea->mode = 0x7;
			ea->reg = 1;
			return pos;
		}
		if (*pos != '(') goto err;
		/* adr reg or pc indexed or offset */
		pos++;
		if ((pos[0] == 'p') && (pos[1] == 'c')) {
			/* pc relative */
			pos += 2;
			if ((pos [0] == ')') && (flags & F_PC_OFFSET)) {
				if ((ea->imm.val < -32768) || (ea->imm.val > 32767)) error ("Immediate value too large.");
				ea->mode = 7;
				ea->reg = 2;
				return ++pos;
			}
			if (!(flags & F_PC_INDEX)) goto err;
			check (',');
			pos = get_reg (pos, &reg, F_AREG | F_DREG);
			ea->ext.ext = 0;
			if (reg > 7) {
				ea->ext._.reg = reg-8;
				ea->ext._.d_or_a = 1;
			} else {
				ea->ext._.reg = reg;
				ea->ext._.d_or_a = 0;
			}
			check ('.');
			if (pos[0] == 'l') {
				ea->ext._.size = 1;
			} else if (pos[0] == 'w') {
				ea->ext._.size = 0;
			} else error ("Index register must have size.");
			pos++;
			check (')');
			ea->mode = 7;
			ea->reg = 3;
			return pos;
		} else {
			/* adr with index/offset */
			pos = get_reg (pos, &ea->reg, F_AREG);
			if ((flags & F_OFFSET) && (pos[0] == ')')) {
				/* with offset only */
				if ((ea->imm.val < -32768) || (ea->imm.val > 32767)) error ("Immediate value too large.");
				ea->mode = 5;
				return ++pos;
			}
			if (!(flags & F_INDEX)) goto err;
	f_index:
			check (',');
			pos = get_reg (pos, &reg, F_AREG | F_DREG);
			if ((ea->imm.val < -128) || (ea->imm.val > 127)) error ("Immediate value too large.");
			ea->ext.ext = 0;
			ea->ext._.displacement = ea->imm.val;
			if (reg > 7) {
				ea->ext._.reg = reg-8;
				ea->ext._.d_or_a = 1;
			} else {
				ea->ext._.reg = reg;
				ea->ext._.d_or_a = 0;
			}
			check ('.');
			if (pos[0] == 'l') {
				ea->ext._.size = 1;
			} else if (pos[0] == 'w') {
				ea->ext._.size = 0;
			} else error ("Index register must have size.");
			pos++;
			check (')');
			ea->mode = 6;
			return pos;
		}	
	}
	error ("Illegal effective address.");
err:
	fprintf (stderr, "(%s)", pos);
	ea_error (flags);
	return NULL;
}

int _reg_num (char *s)
{
	int is_areg;
	int num;
	
	if (s[0] == 'a') is_areg = 8;
	else is_areg = 0;
	s++;

	if (sscanf (s, "%d", &num) != 1) return -1;
	num += is_areg;

	while (isdigit (*s)) s++;
	if (isalpha (*s)) return -1;
	return num;
}

char *parse_movm (char *pos, int size)
{
	short mask;
	struct ImmVal imm;
	union Opcode op;
	int dr = 0; /* reg to mem */
	ea_t ea;
	
	if (pos[0] == '#') {
		/* registers to memory */
		pos++;
		pos = get_imm (pos, &imm, F_NOLABELS);
		mask = imm.val;
		check (',');
		pos = get_ea (pos, &ea, size, F_IND | F_PRE | F_INDEX | F_OFFSET | F_MEM_W | F_MEM_L);
	} else {
		/* memory to registers */
		dr = 1;
		pos = get_ea (pos, &ea, size, F_IND | F_POST | F_INDEX | F_OFFSET | F_MEM_W | F_MEM_L | F_PC_OFFSET | F_PC_INDEX);
		check (',');
		check ('#');
		pos = get_imm (pos, &imm, F_NOLABELS);
		mask = imm.val;
	}
		
	op.code = 0x4880;
	op.movem.sz = (size == WORD ? 0 : 1);
	op.movem.dr = dr;
	op.movem.dest_reg = ea.reg;
	op.movem.dest_mode = ea.mode;
	wr_short (op.code);

	wr_short (mask);
	/* movem seems to count offsets relative to the
	 * register bitfield... */
	last_op_addr += 2;
	wr_ea (&ea);
	return pos;
}
	
char *parse_movem (char *pos, int size)
{
	union Opcode op;
	char *start = pos;
	char lab1[LAB_LEN];
	int lo, hi;
	/* d0-7,a0-7 */
	int regs[16];
	int dr = 0; /* reg to mem */
	ea_t ea;

    // register mask as immediate
    if (strstr(pos, "#")) {
        return parse_movm (pos, size);
    }

	memset (regs, 0, 16*sizeof(int));
try_again:
	if ((pos[0] == 'a') || (pos[0] == 'd')) {
more_to_come:
		pos = rd_label (pos, lab1);
		lo = _reg_num (lab1);
		if (lo == -1) {
mess:
			/* mem to reg */
			dr = 1; 
			pos = start;
			pos = get_ea (pos, &ea, size, F_IND | F_POST | F_INDEX | F_OFFSET | F_MEM_W | F_MEM_L | F_PC_OFFSET | F_PC_INDEX);
			if (pos[0] != ',') goto err;
			pos++;
			goto try_again;
		}
		
		if (*pos == '-') {
			/* range of registers */
			pos++;
			if (sscanf (pos, "%d", &hi) != 1) goto err;
			pos++;
			if ((hi < 0) || (hi > 7)) goto err;
			if (lo > 7) hi += 8;
		} else {
			hi = lo;
		}
		for (; lo<=hi; lo++) regs[lo] = 1;
		if (pos[0] == '/') {
			pos++;
			goto more_to_come;
		}
	} else {
		goto mess;
	}
	if (!dr) {
		/* reg to mem */
		if (pos[0] != ',') goto err;
		pos++;
		pos = get_ea (pos, &ea, size, F_IND | F_PRE | F_INDEX | F_OFFSET | F_MEM_W | F_MEM_L);
	}
	op.code = 0x4880;
	op.movem.sz = (size == WORD ? 0 : 1);
	op.movem.dr = dr;
	op.movem.dest_reg = ea.reg;
	op.movem.dest_mode = ea.mode;
	wr_short (op.code);

	hi = 0;
	if (ea.mode == 0x4) {
		/* predecrement mode */
		for (lo=0; lo < 16; lo++) {
			if (regs[15-lo]) hi |= (1<<lo);
		}
	} else {
		for (lo=0; lo < 16; lo++) {
			if (regs[lo]) hi |= (1<<lo);
		}
	}
	wr_short (hi);
	/* movem seems to count offsets relative to the
	 * register bitfield... */
	last_op_addr += 2;
	wr_ea (&ea);
	return pos;
err:
	error ("your movem is all wanked up.");
	return NULL;
}

int asm_pass1 ()
{
	int i, reg, size;
	union Opcode op;
	struct ImmVal imm;
	char lab1[LAB_LEN];
	char *equ_lab;
	char *pos;
	ea_t ea;
	ea_t ea2;

	wr_short (0x601a);
	wr_int (0);
	wr_int (0);
	wr_int (0);
	wr_int (0);
	wr_int (0);
	wr_int (0);
	wr_short (0);

	dict_init (&labels);
	
	/* gas output doesn't have main first, so the first thing we do is jmp
	 * to wherever main begins... */
	memcpy (lab1, "main\n", sizeof (lab1));
	get_ea (lab1, &ea, LONG, F_MEM_L);
	op.code = 0x4ec0;
	op.type1.ea_reg = ea.reg;
	op.type1.ea_mode = ea.mode;
	wr_short (op.code);
	wr_ea (&ea);
	
	while (fgets (buf, sizeof (buf), fin)) {
		last_op_addr = get_bitpos ();
		line_no++;
		pos = &buf[0];
		/* comments */
		equ_lab = NULL;
		while (isspace (*pos)) pos++;
		if (*pos == '\0') continue;
		if (*pos == '*') continue;
		if (*pos == ';') continue;
		else if (IS_START_OF_LABEL (*pos)) {
			equ_lab = pos;
			pos = rd_label (pos, lab1);
			if (*pos == ':') {
				/* add label */
				add_label (lab1, L_ADDR, get_bitpos () - BASE);
				if (dump_labels) {
					printf ("0x%x: %s\n", get_bitpos () - BASE, lab1);
				}
				equ_lab = NULL;
				pos++;
				check_whitespace ();
				if (*pos == '\0') continue;
			}
			while (isspace (*pos)) pos++;
		}
		/* EQU */
		if (strncmp (pos, "equ", 3) == 0) {
			pos += 3;
			check_whitespace();
			if (!equ_lab) error ("EQU without label.");
			pos = get_imm (pos, &imm, F_NOLABELS);
			add_label (lab1, L_CONST, imm.val);
			continue;
		}
		if (equ_lab) {
			pos = equ_lab;
			equ_lab = NULL;
		}
	
		if (strncmp (pos, ".comm", 5) == 0) {
			pos += 5;
			check_whitespace ();
			pos = rd_label (pos, lab1);
			add_label (lab1, L_ADDR, get_bitpos () - BASE);
			check (',');
			pos = get_imm (pos, &imm, F_NOLABELS);
			for (i=0; i<imm.val; i++) {
				wr_byte (0);
			}
			check (',');
			goto align;
		}
		
		if (strncmp (pos, ".long", 5) == 0) {
			pos += 5;
			check_whitespace ();
			pos = get_ea (pos, &ea, LONG, F_MEM_L);
			check_end (pos);
			wr_ea (&ea);
			continue;
		}
		
		if (strncmp (pos, ".word", 5) == 0) {
			pos += 5;
			check_whitespace ();
			pos = get_imm (pos, &imm, F_NOLABELS);
			check_end (pos);
			wr_short (imm.val);
			continue;
		}
		
		if (strncmp (pos, ".byte", 5) == 0) {
			pos += 5;
			check_whitespace ();
			pos = get_imm (pos, &imm, F_NOLABELS);
			check_end (pos);
			wr_byte (imm.val);
			continue;
		}
		
		/* gas .string */
		if (strncmp (pos, ".string", 7) == 0) {
			pos += 7;
			check_whitespace ();

			check ('"');
			for (i=0;;i++) {
				if (*pos == '"') {
					pos++;
					check_end (pos);
					break;
				} else if (*pos == '\\') {
					pos++;
					switch (*pos) {
						case 'n': wr_byte ('\n'); break;
						case 'r': wr_byte ('\r'); break;
						case 'f': wr_byte ('\f'); break;
						case '"': wr_byte ('"'); break;
						default:
							  error ("Unrecognised .string escape char.");
							  break;
					}
					pos++;
				} else {
					wr_byte (*pos);
					pos++;
				}
			}
			wr_byte (0);
			i++;
			/* maintain word alignment */
			if (i & 0x1) wr_byte (0);
			continue;
		}
		if (strncmp (pos, ".align", 6) == 0) {
			pos += 6;
			check_whitespace ();
		align:	
			pos = get_imm (pos, &imm, F_NOLABELS);
			check_end (pos);

			i = get_bitpos ();
			imm.val = i % imm.val;
			for (i=0; i<imm.val; i++) {
				wr_byte (0);
			}
			continue;
		}
		/* DC.X */
		if (strncmp (pos, "dc.", 3) == 0) {
			size = get_size (pos[3]);
			pos += 4;
			check_whitespace();
			if ((size == BYTE) && (*pos == '"')) {
				thing:
				pos++;
				while (*pos != '"') {
					wr_byte (*pos);
					pos++;
				}
				/* double '""' escapes */
				pos++;
				if (*pos == '"') {
					wr_byte ('"');
					goto thing;
				}
				while (isspace (*pos)) pos++;
				if (*pos != ',') {
					check_end (pos);
					continue;
				}
				pos++;
			}
			while (isdigit (*pos) || isalpha (*pos) || (*pos == '$')) {
				pos = get_imm (pos, &imm, 0);
				if (imm.has_label) {
					switch (size) {
						case BYTE: case WORD: error ("Labels may only be used with dc.l.");
						case LONG: default:
							wr_int (0);
							add_fixup (get_bitpos()-4, LONG_FIXUP, imm.label, imm.gas_offset);
							break;
					}
				} else {
					switch (size) {
						case BYTE: wr_byte (imm.val); break;
						case WORD: wr_short (imm.val); break;
						case LONG: default: wr_int (imm.val); break;
					}
				}
				while (isspace (*pos)) pos++;
				if (*pos != ',') break;
				pos++;
				while (isspace (*pos)) pos++;
			}
			check_end (pos);
			continue;
		}
		/* DS.X */
		if (strncmp (pos, "ds.", 3) == 0) {
			size = get_size (pos[3]);
			pos += 4;
			check_whitespace();
			pos = get_imm (pos, &imm, F_NOLABELS);
			check_end (pos);
			for (i=0; i<imm.val; i++) {
				if (size == BYTE) wr_byte (0);
				else if (size == WORD) wr_short (0);
				else wr_int (0);
			}
			continue;	
		}
		if (strncmp (pos, ".zero", 5) == 0) {
			pos += 5;
			check_whitespace ();
			pos = get_imm (pos, &imm, F_NOLABELS);
			check_end (pos);
			for (i=0; i<imm.val; i++) {
				wr_byte (0);
			}
			continue;
		}
			
		/* GCC crap we ignore */
		if ((strncmp (pos, ".file", 5) == 0) ||
		    (strncmp (pos, ".version", 8) == 0) ||
		    (strncmp (pos, ".text", 5) == 0) ||
		    (strncmp (pos, ".globl", 6) == 0) ||
		    (strncmp (pos, ".type", 5) == 0) ||
		    (strncmp (pos, ".size", 5) == 0) ||
		    (strncmp (pos, ".ident", 6) == 0) ||
		    (strncmp (pos, ".section", 8) == 0) ||
		    (strncmp (pos, ".data", 5) == 0) ||
		    (strncmp (pos, ".local", 6) == 0) ||
		    (strncmp (pos, "| ", 2) == 0) ||
		    (strncmp (pos, "#APP", 4) == 0) ||
		    (strncmp (pos, "#NO_APP", 7) == 0)) {
			continue;
		}
		/* odd address checking */
		if (last_op_addr & 0x1) error ("Odd address.");
		/* ADDQ/SUBQ */
		if ((strncmp (pos, "addq", 4)==0) ||
		    (strncmp (pos, "subq", 4)==0)) {
			op.code = 0x5000;
			op.addq.issub = (pos[0] == 's');
			pos += 4;
			check ('.');
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_imm (pos, &imm, F_NOLABELS | F_IMM);
			if ((imm.val < 1) || (imm.val > 8)) error ("Immediate value too large.");
			if (imm.val == 8) imm.val = 0;
			check (',');
			pos = get_ea (pos, &ea, LONG, F_DREG | F_AREG | F_IND | F_POST | F_PRE | F_INDEX | F_OFFSET | F_MEM_W | F_MEM_L);
			if ((size == BYTE) && (ea.mode == 1)) error ("Bad size for address reg direct.");
			check_end (pos);
			op.addq.dest_reg = ea.reg;
			op.addq.dest_mode = ea.mode;
			op.addq.size = size;
			op.addq.data = imm.val;
			wr_short (op.code);
			wr_ea (&ea);
			continue;
		}
		/* NOP */
		if (strncmp (pos, "nop", 3) == 0) {
			pos += 3;
			check_end (pos);
			wr_short (0x4e71);
			continue;
		}
		/* NOT/NEGX/NEG */
		if (strncmp (pos, "not", 3) == 0) {
			pos += 3;
			check ('.');
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_NOIMM_NOPC_NOAREG);
			check_end (pos);
			op.code = 0x4600;
			op.type1.size = size;
			op.type1.ea_reg = ea.reg;
			op.type1.ea_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			continue;
		}
		if (strncmp (pos, "negx", 4) == 0) {
			pos += 4;
			check ('.');
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_NOIMM_NOPC_NOAREG);
			check_end (pos);
			op.code = 0x4000;
			op.type1.size = size;
			op.type1.ea_reg = ea.reg;
			op.type1.ea_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			continue;
		}
		if (strncmp (pos, "neg", 3) == 0) {
			pos += 3;
			check ('.');
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_NOIMM_NOPC_NOAREG);
			check_end (pos);
			op.code = 0x4400;
			op.type1.size = size;
			op.type1.ea_reg = ea.reg;
			op.type1.ea_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			continue;
		}
		/* DIVS/DIVU */
		if ((strncmp (pos, "divs", 4) == 0) ||
		    (strncmp (pos, "divu", 4) == 0)) {
			if (pos[3] == 's') op.code = 0x81c0;
			else if (pos[3] == 'u') op.code = 0x80c0;
			pos += 4;
			if (*pos == '.') {
				check ('.');
				check ('w');
			}
			check_whitespace();
			pos = get_ea (pos, &ea, WORD, F_ALL_NOAREG);
			check (',');
			pos = get_reg (pos, &i, F_DREG);
			check_end (pos);
			op.type2.reg = i;
			op.type2.ea_mode = ea.mode;
			op.type2.ea_reg = ea.reg;
			wr_short (op.code);
			wr_ea (&ea);
			continue;
		}
		/* MULS/MULU */
		if ((strncmp (pos, "muls", 4) == 0) ||
		    (strncmp (pos, "mulu", 4) == 0)) {
			if (pos[3] == 's') op.code = 0xc1c0;
			else if (pos[3] == 'u') op.code = 0xc0c0;
			pos += 4;
			if (*pos == '.') {
				check ('.');
				check ('w');
			}
			check_whitespace();
			pos = get_ea (pos, &ea, WORD, F_ALL_NOAREG);
			check (',');
			pos = get_reg (pos, &i, F_DREG);
			check_end (pos);
			op.type2.reg = i;
			op.type2.ea_mode = ea.mode;
			op.type2.ea_reg = ea.reg;
			wr_short (op.code);
			wr_ea (&ea);
			continue;
		}
		/* ADDI/SUBI */
		if ((strncmp (pos, "addi", 4) == 0) ||
		    (strncmp (pos, "subi", 4) == 0)) {
			if (pos[0] == 'a') op.code = 0x0600;
			else op.code = 0x0400;
			pos += 4;
			check ('.');
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea2, size, F_IMM);
			check (',');
			pos = get_ea (pos, &ea, size, F_NOIMM_NOPC_NOAREG);
oops_its_an_addi_subi:
			check_end (pos);
			op.type1.size = size;
			op.type1.ea_reg = ea.reg;
			op.type1.ea_mode = ea.mode;
			wr_short (op.code);
			if (size == BYTE) {
				wr_short (ea2.imm.val & 0xff);
			} else if (size == WORD) {
				wr_short (ea2.imm.val);
			} else {
				wr_int (ea2.imm.val);
			}
			wr_ea (&ea);
			continue;
		}
		/* ADDA/SUBA */
		if ((strncmp (pos, "adda", 4) == 0) ||
		    (strncmp (pos, "suba", 4) == 0)) {
			if (pos[0] == 'a') op.code = 0xd000;
			else op.code = 0x9000;
			pos += 4;
			check ('.');
			size = get_size (pos[0]);
			if (size == BYTE) error ("Crap size for adda.");
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_ALL);
			check (',');
			pos = get_reg (pos, &reg, F_AREG);
			check_end (pos);
			op.type2.reg = reg;
			if (size == WORD) size = 3;
			else size = 7;
			op.type2.op_mode = size;
			op.type2.ea_reg = ea.reg;
			op.type2.ea_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			continue;
		}
		/* ADDX/SUBX */
		if ((strncmp (pos, "addx", 4) == 0) ||
		    (strncmp (pos, "subx", 4) == 0)) {
			if (pos[0] == 'a') op.code = 0xd100;
			else op.code = 0x9100;
			pos  += 4;
			check ('.');
			size = get_size (pos[0]);
			op.addx.size = size;
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_DREG | F_PRE);
			if (ea.mode == 0) {
				/* Dn,Dn mode */
				check (',');
				op.addx.src_reg = ea.reg;
				op.addx.rm = 0;
				pos = get_reg (pos, &i, F_DREG);
				check_end (pos);
				op.addx.dest_reg = i;
				wr_short (op.code);
			} else {
				/* -(An),-(An) mode */
				check (',');
				op.addx.src_reg = ea.reg;
				op.addx.rm = 1;
				pos = get_ea (pos, &ea, size, F_PRE);
				op.addx.dest_reg = ea.reg;
				wr_short (op.code);
			}
			check_end (pos);
			continue;
		}
		/* ADD/SUB */
		if ((strncmp (pos, "add", 3) == 0) ||
		    (strncmp (pos, "sub", 3) == 0)) {
			if (pos[0] == 'a') op.code = 0xd000;
			else op.code = 0x9000;
			pos += 3;
			check ('.');
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_ALL);
			check (',');
			pos = get_ea (pos, &ea2, size, F_DREG | F_AREG | F_IND | F_POST | F_PRE | F_OFFSET | F_INDEX | F_MEM_W | F_MEM_L);
			if (ea2.mode == 0) {
				/* data reg dest */
				op.type2.reg = ea2.reg;
				op.type2.op_mode = size;
				op.type2.ea_reg = ea.reg;
				op.type2.ea_mode = ea.mode;
				wr_short (op.code);
				wr_ea (&ea);
			} else if (ea2.mode == 1) {
				/* adda */
				if (size == BYTE) error ("Add to address reg can't be size byte");
				else if (size == WORD) size = 3;
				else size = 7;
				op.type2.reg = ea2.reg;
				op.type2.op_mode = size;
				op.type2.ea_reg = ea.reg;
				op.type2.ea_mode = ea.mode;
				wr_short (op.code);
				wr_ea (&ea);
			} else {
                if (ea.mode == 7) {
                    /* it's really an addi/subi instruction */
                    // XXX should check ea2 is properly constrained...
                    op.code = op.code == 0xd000 ? 0x0600 : 0x0400;
                    goto oops_its_an_addi_subi;
                }
				if (ea.mode != 0) error ("One operand must be a data register.");
				op.type2.reg = ea.reg;
				op.type2.op_mode = size+4;
				op.type2.ea_reg = ea2.reg;
				op.type2.ea_mode = ea2.mode;
				wr_short (op.code);
				wr_ea (&ea2);
			}
			check_end (pos);
			continue;
		}
		/* BCHG/BCLR/BSET/BTST */
		if ((strncmp (pos, "bchg", 4)==0) ||
		    (strncmp (pos, "bclr", 4)==0) ||
		    (strncmp (pos, "bset", 4)==0) ||
		    (strncmp (pos, "btst", 4)==0)) {
			int type;
			switch (pos[2]) {
				/* bchg */
				case 'h': type = 0; break;
				/* bclr */
				case 'l': type = 1; break;
				/* bset */
				case 'e': type = 2; break;
				/* btst */
				default: case 's': type = 3; break;
			}
			pos += 4;
			check_whitespace();
			pos = get_ea (pos, &ea, BYTE, F_IMM | F_DREG);
			if (ea.mode == 0) {
				/* Dn,<ea> mode */
				switch (type) {
					case 0: op.code = 0x0140; break;
					case 1: op.code = 0x0180; break;
					case 2: op.code = 0x01c0; break;
					case 3: op.code = 0x0100; break;
				}
				op.type2.reg = ea.reg;
				check (',');
				pos = get_ea (pos, &ea, size, F_NOIMM_NOPC_NOAREG);
				op.type2.ea_reg = ea.reg;
				op.type2.ea_mode = ea.mode;
				wr_short (op.code);
				wr_ea (&ea);
				continue;
			} else {
				/* IMM,<ea> mode */
				check_range (&ea.imm.val, BYTE);
				imm.val = ea.imm.val;
				switch (type) {
					case 0: op.code = 0x0840; break;
					case 1: op.code = 0x0880; break;
					case 2: op.code = 0x08c0; break;
					case 3: op.code = 0x0800; break;
				}
				check (',');
				if (type == 3) {
					pos = get_ea (pos, &ea, size, F_DREG | F_IND | F_POST | F_PRE | F_OFFSET | F_INDEX | F_MEM_W | F_MEM_L | F_PC_OFFSET | F_PC_INDEX);
				} else {
					pos = get_ea (pos, &ea, size, F_NOIMM_NOPC_NOAREG);
				}
				op.type2.ea_mode = ea.mode;
				op.type2.ea_reg = ea.reg;
				wr_short (op.code);
				wr_short (imm.val & 0xff);
				wr_ea (&ea);
				continue;
			}
			check_end (pos);
			continue;
		}
		/* ASx/LSx/ROx ROXx */
		if ((strncmp (pos, "ls", 2) == 0) ||
		    (strncmp (pos, "as", 2) == 0) ||
		    (strncmp (pos, "ro", 2) == 0)) {
			char dir;
			int type;
			dir = pos[2];
			if (pos[0] == 'a') type = 0;
			else if (pos[0] == 'l') type = 1;
			else if (pos[0] == 'r') {
				if (pos[2] == 'x') {
					type = 2;
					dir = pos[3];
				}
				else type = 3;
			} else {
				goto blork;
			}
			if ((dir != 'l') && (dir != 'r')) goto blork;
			dir = (dir == 'l' ? 1 : 0);
			while (isalpha (*pos)) pos++;
			check ('.');
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_DREG | F_IND | F_POST | F_PRE | F_OFFSET | F_INDEX | F_MEM_W | F_MEM_L | F_IMM);
			if (ea.mode == 0) {
				/* data reg type */
				check (',');
				pos = get_reg (pos, &reg, F_DREG);
				check_end (pos);
				if (type == 1) op.code = 0xe008;
				else if (type == 0) op.code = 0xe000;
				else if (type == 2) op.code = 0xe010;
				else op.code = 0xe018;
				op.ASx.count_reg = ea.reg;
				op.ASx.dr = dir;
				op.ASx.size = size;
				op.ASx.ir = 1;
				op.ASx.reg = reg;
				wr_short (op.code);
				continue;
			} else if ((ea.mode == 7) && (ea.reg == 4)) {
				/* imm,Dn type */
				check (',');
				pos = get_reg (pos, &reg, F_DREG);
				check_end (pos);
				if (type == 1) op.code = 0xe008;
				else if (type == 0) op.code = 0xe000;
				else if (type == 2) op.code = 0xe010;
				else op.code = 0xe018;
				if ((ea.imm.val < 1) || (ea.imm.val > 8)) error ("Bad immediate value.");
				if (ea.imm.val == 8) ea.imm.val = 0;
				op.ASx.count_reg = ea.imm.val;
				op.ASx.dr = dir;
				op.ASx.size = size;
				op.ASx.ir = 0;
				op.ASx.reg = reg;
				wr_short (op.code);
				continue;
			} else {
				/* ea mode */
				if (size != WORD) error ("Illegal size.");
				check_end (pos);
				if (type == 1) op.code = 0xe2c0;
				else if (type == 0) op.code = 0xe0c0;
				else if (type == 2) op.code = 0xe4c0;
				else op.code = 0xe6c0;
				op.ASx.dr = dir;
				op.type1.ea_reg = ea.reg;
				op.type1.ea_mode = ea.mode;
				wr_short (op.code);
				wr_ea (&ea);
				continue;
			}
			continue;
		}
blork:
		/* ANDI/EORI/ORI */
		if ((strncmp (pos, "andi", 4) == 0) ||
		    (strncmp (pos, "eori", 4) == 0) ||
		    (strncmp (pos, "ori", 3) == 0)) {
			if (pos[0] == 'a') {
				op.code = 0x0200;
			} else if (pos[0] == 'e') {
				op.code = 0x0a00;
			} else {
				op.code = 0x0000;
			}
			while (isalpha (*pos)) pos++;
			check ('.');
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_imm (pos, &imm, F_NOLABELS | F_IMM);
			check_range (&imm.val, size);
			check (',');
			pos = get_ea (pos, &ea, size, F_NOIMM_NOPC_NOAREG);
			check_end (pos);
			op.type1.ea_reg = ea.reg;
			op.type1.ea_mode = ea.mode;
			op.type1.size = size;
			wr_short (op.code);
			if (size == BYTE) {
				wr_short (imm.val & 0xff);
			} else if (size == WORD) {
				wr_short (imm.val);
			} else {
				wr_int (imm.val);
			}
			wr_ea (&ea);
			continue;
		}
		/* AND/OR */
		if ((strncmp (pos, "and", 3) == 0) ||
		    (strncmp (pos, "or", 2) == 0)) {
			if (pos[0] == 'a') op.code = 0xc000;
			else op.code = 0x8000;
			while (isalpha (*pos)) pos++;
			check ('.');
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_ALL_NOAREG);
			check (',');
			pos = get_ea (pos, &ea2, size, F_NOIMM_NOPC_NOAREG);
			check_end (pos);
			if (ea2.mode == 0) {
				/* data reg dest */
				op.type2.reg = ea2.reg;
				op.type2.op_mode = size;
				op.type2.ea_reg = ea.reg;
				op.type2.ea_mode = ea.mode;
				wr_short (op.code);
				wr_ea (&ea);
			} else {
				/* ea dest */
				if (ea.mode != 0) error ("One operand must be a data register.");
				op.type2.reg = ea.reg;
				op.type2.op_mode = size+4;
				op.type2.ea_reg = ea2.reg;
				op.type2.ea_mode = ea2.mode;
				wr_short (op.code);
				wr_ea (&ea2);
			}
			continue;
		}
		/* EOR */
		if (strncmp (pos, "eor", 3) == 0) {
			op.code = 0xb000;
			pos += 3;
			check ('.');
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_reg (pos, &reg, F_DREG);
			check (',');
			pos = get_ea (pos, &ea, size, F_NOIMM_NOPC_NOAREG);
			check_end (pos);
			op.type2.reg = reg;
			size += 4;
			op.type2.op_mode = size;
			op.type2.ea_reg = ea.reg;
			op.type2.ea_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			continue;
		}
		/* SWAP */
		if (strncmp (pos, "swap", 4) == 0) {
			pos += 4;
			pos = get_reg (pos, &reg, F_DREG);
			op.code = 0x4840;
			op.type2.ea_reg = reg;
			check_end (pos);
			wr_short (op.code);
			continue;
		}
		/* BSR */
		if (strncmp (pos, "bsr", 3) == 0) {
			pos += 3;
			check ('.');
			size = pos[0];
			pos++;
			check_whitespace();
			pos = rd_label (pos, lab1);
			check_end (pos);
			op.code = 0x6100;
			wr_short (op.code);
			if (size == 's') {
				add_fixup (get_bitpos()-1, BYTE, lab1, 0);
			} else if (size == 'w') {
				wr_short (0);
				add_fixup (get_bitpos()-2, WORD, lab1, 0);
			} else {
				error ("Invalid size '.%c'.", size);
			}
			continue;
		}
		/* JSR */
		if (strncmp (pos, "jsr", 3) == 0) {
			pos += 3;
		//jsr:
			check_whitespace();
			pos = get_ea (pos, &ea, LONG, F_IND | F_OFFSET |
					F_INDEX | F_MEM_W | F_MEM_L | F_PC_OFFSET | F_PC_INDEX);
			check_end (pos);
			op.code = 0x4e80;
			op.type1.ea_reg = ea.reg;
			op.type1.ea_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			continue;
		}
		/* JMP */
		if (strncmp (pos, "jmp", 3) == 0) {
			pos += 3;
			check_whitespace();
			pos = get_ea (pos, &ea, LONG, F_IND | F_OFFSET |
					F_INDEX | F_MEM_W | F_MEM_L | F_PC_OFFSET | F_PC_INDEX);
			check_end (pos);
			op.code = 0x4ec0;
			op.type1.ea_reg = ea.reg;
			op.type1.ea_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			continue;
		}
		/*if (strncmp (pos, "jbsr", 4)==0) {
			pos += 4;
			goto jsr;
		}*/
		if (strncmp (pos, "j", 1)==0) {
			pos += 1;
			for (i=0; i<16; i++) {
				if (Bcc_str [i] == NULL) continue;
				if (strncmp (pos, Bcc_str[i], 2)==0) break;
			}
			if (i != 16) {
				size = 'w';
				pos += 2;
				goto bra;
			}
		}
		/* Bcc.S */
		if (pos[0] == 'b') {
			for (i=0; i<16; i++) {
				if (Bcc_str [i] == NULL) continue;
				if (strncmp (&pos[1], Bcc_str[i], 2)==0) break;
			}
			if (i==16) goto skip;
			pos += 3;
			check ('.');
			size = pos[0];
			pos++;
		bra:	check_whitespace();
			pos = rd_label (pos, lab1);
			check_end (pos);
			op.code = 0x6000 | (i<<8);
			wr_short (op.code);
			if (size == 's') {
				add_fixup (get_bitpos()-1, BYTE, lab1, 0);
			} else if (size == 'w') {
				wr_short (0);
				add_fixup (get_bitpos()-2, WORD, lab1, 0);
			} else {
				error ("Invalid size '.%c'.", size);
			}
			continue;
		}
		/* DBcc */
		if ((pos[0] == 'd') && (pos[1] == 'b')) {
			pos = rd_label (&pos[2], lab1);
			for (i=0; i<16; i++) {
				if (strncmp (lab1, DBcc_str[i], 2)==0) break;
			}
			if (i==16) {
				if (strncmp (lab1, "ra", 2)==0) {
					i = 1;
				} else {
					goto skip;
				}
			}
			while (isalpha (*pos)) pos++;
			check_whitespace();
			pos = get_reg (pos, &reg, F_DREG);
			check (',');
			pos = rd_label (pos, lab1);
			check_end (pos);
			op.code = 0x50c8;
			op.DBcc.reg = reg;
			op.DBcc.cond = i;
			wr_short (op.code);
			wr_short (0);
			add_fixup (get_bitpos()-2, WORD, lab1, 0);
			continue;
		}
skip:
		/* CLR */
		if (strncmp (pos, "clr", 3) == 0) {
			pos += 3;
			check ('.');
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_NOIMM_NOPC_NOAREG);
			check_end (pos);
			op.code = 0x4200;
			op.type1.size = size;
			op.type1.ea_reg = ea.reg;
			op.type1.ea_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			continue;
		}
		/* CMPI */
		if (strncmp (pos, "cmpi", 4) == 0) {
			pos += 4;
			check ('.');
			size = get_size (*pos);
			pos++;
			check_whitespace();
			pos = get_imm (pos, &imm, F_NOLABELS | F_IMM);
			check_range (&imm.val, size);
			check (',');
			pos = get_ea (pos, &ea, size, F_NOIMM_NOPC_NOAREG);
			check_end (pos);
			op.code = 0x0c00;
			op.type1.ea_reg = ea.reg;
			op.type1.ea_mode = ea.mode;
			op.type1.size = size;
			wr_short (op.code);
			if (size == BYTE) {
				wr_short (imm.val & 0xff);
			} else if (size == WORD) {
				wr_short (imm.val);
			} else {
				wr_int (imm.val);
			}
			wr_ea (&ea);
			continue;
		}
		/* CMPA */
		if (strncmp (pos, "cmpa", 4) == 0) {
			pos += 4;
			check ('.');
			size = get_size (*pos);
			pos++;
			if (size == BYTE) error ("Size must be word or long.");
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_ALL);
			check (',');
			pos = get_reg (pos, &i, F_AREG);
			check_end (pos);

			op.code = 0xb000;
			op.type2.reg = i;
			op.type2.op_mode = (size == WORD ? 3 : 7);
			op.type2.ea_mode = ea.mode;
			op.type2.ea_reg = ea.reg;
			wr_short (op.code);
			wr_ea (&ea);
			continue;
		}
		/* CMPM */
		if (strncmp (pos, "cmpm", 4) == 0) {
			pos += 4;
			check ('.');
			size = get_size (*pos);
			pos++;
			check_whitespace ();
			pos = get_ea (pos, &ea, size, F_POST);
			op.code = 0xb108;
			op.cmpm.src_reg = ea.reg;
			op.cmpm.size = size;
			check (',');
			pos = get_ea (pos, &ea, size, F_POST);
			op.cmpm.dest_reg = ea.reg;
			wr_short (op.code);
			continue;
		}
		/* CMP */
		if (strncmp (pos, "cmp", 3) == 0) {
			pos += 3;
			check ('.');
			size = get_size (*pos);
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_ALL);
			check (',');
			pos = get_ea (pos, &ea2, size, F_NOIMM_NOPC);
			check_end (pos);

			if (ea2.mode == MODE_DREG) {
				/* cmp <EA>,Dx */
				if ((size == BYTE) && (ea.mode == 0x1))
					error ("Bad size for address register gropery.");
				op.code = 0xb000;
				op.type2.reg = ea2.reg;
				op.type2.op_mode = size;
				op.type2.ea_mode = ea.mode;
				op.type2.ea_reg = ea.reg;
				wr_short (op.code);
				wr_ea (&ea);
			} else if (ea2.mode == MODE_AREG) {
				/* cmpa <EA>,Ax */
				if (size == BYTE) error ("Size must be word or long.");
				op.code = 0xb000;
				op.type2.reg = ea2.reg;
				op.type2.op_mode = (size == WORD ? 3 : 7);
				op.type2.ea_mode = ea.mode;
				op.type2.ea_reg = ea.reg;
				wr_short (op.code);
				wr_ea (&ea);
			} else if ((ea.mode == MODE_EXT) && (ea.reg == EXT_IMM)) {
				/* cmpi #val,<ea> */
				check_ea (&ea2, size, F_DREG | F_IND | F_POST | F_PRE | F_OFFSET | F_INDEX | F_MEM_W | F_MEM_L);
				op.code = 0x0c00;
				op.type1.ea_reg = ea2.reg;
				op.type1.ea_mode = ea2.mode;
				op.type1.size = size;
				wr_short (op.code);
				if (size == BYTE) {
					wr_short (ea.imm.val & 0xff);
				} else if (size == WORD) {
					wr_short (ea.imm.val);
				} else {
					wr_int (ea.imm.val);
				}
				wr_ea (&ea2);
			} else if ((ea.mode == MODE_POST) && (ea2.mode == MODE_POST)) {
				/* cmpm (Ay)+,(Ax)+ */
				op.code = 0xb108;
				op.cmpm.src_reg = ea.reg;
				op.cmpm.size = size;
				op.cmpm.dest_reg = ea2.reg;
				wr_short (op.code);
			} else {
				error ("Invalid operands for cmp instruction.");
			}
			continue;
		}
		/* EXG */
		if (strncmp (pos, "exg", 3) == 0) {
			pos += 3;
			op.code = 0xc100;
			check_whitespace();
			pos = get_reg (pos, &reg, F_DREG | F_AREG);
			check (',');
			if (reg > 7) {
				/* Ax,Ay mode */
				op.exg.op_mode = 0x9;
				op.exg.src_reg = reg;
				pos = get_reg (pos, &reg, F_AREG);
				op.exg.dest_reg = reg;
			} else {
				/* Dx,Xn mode */
				op.exg.src_reg = reg;
				pos = get_reg (pos, &reg, F_AREG | F_DREG);
				if (reg > 7) {
					op.exg.op_mode = 0x11;
				} else {
					op.exg.op_mode = 0x8;
				}
				op.exg.dest_reg = reg;
			}
			check_end (pos);
			wr_short (op.code);
			continue;
		}
		/* EXT */
		if (strncmp (pos, "ext", 3) == 0) {
			pos += 3;
			check ('.');
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			if (size == BYTE) error ("Ext dislikes bytes.");
			pos = get_reg (pos, &reg, F_DREG);
			check_end (pos);
			op.code = 0x4800;
			op.type2.ea_reg = reg;
			op.type2.op_mode = size+1;
			wr_short (op.code);
			continue;
		}
			
		/* gas MOVM: movem with simple integer regmasks */
		if (strncmp (pos, "movm.", 5) == 0) {
			pos += 5;
			size = get_size (pos[0]);
			pos++;
			if (size == BYTE) error  ("Movem dislikes bytes.");
			check_whitespace ();
			pos = parse_movm (pos, size);
			check_end (pos);
			continue;
		}
		/* MOVEM */
		if (strncmp (pos, "movem.", 6)==0) {
			pos += 6;
			size = get_size (pos[0]);
			pos++;
			if (size == BYTE) error  ("Movem dislikes bytes.");
			check_whitespace();
			/* register to memory */
			pos = parse_movem (pos, size);
			check_end (pos);
			continue;
		}
		/* RTE */
		if (strncmp (pos, "rte", 3) == 0) {
			check_end (&pos[3]);
			wr_short (0x4e73);
			continue;
		}
		/* RTS */
		if (strncmp (pos, "rts", 3) == 0) {
			check_end (&pos[3]);
			wr_short (0x4e75);
			continue;
		}
		/* ILLEGAL */
		if (strncmp (pos, "illegal", 7) == 0) {
			check_end (&pos[7]);
			wr_short (0x4afc);
			continue;
		}
		/* HOSTCALL -- Not 68000 */
		if (strncmp (pos, "hostcall", 8) == 0) {
			pos += 8;
			check_end (pos);
			wr_short (0x000a);
			continue;
		}
		if (strncmp (pos, "hcall", 5) == 0) {
			pos += 5;
			check_whitespace();
			pos = get_imm (pos, &imm, F_IMM | F_NOLABELS);
			check_end (pos);
			check_range (&imm.val, WORD);
			wr_short (0x000b);
			wr_short (imm.val);
			continue;
		}
		/* RESET */
		if (strncmp (pos, "reset", 5) == 0) {
			check_end (&pos[5]);
			wr_short (0x4e70);
			continue;
		}
		/* TRAP */
		if (strncmp (pos, "trap", 4) == 0) {
			/* TRAPV */
			if (pos[4] == 'v') {
				check_end (&pos[5]);
				wr_short (0x4e76);
				continue;
			}
			pos += 4;
			check_whitespace();
			pos = get_imm (pos, &imm, F_IMM | F_NOLABELS);
			check_end (pos);
			if ((imm.val < 0) || (imm.val > 15)) {
				error ("Trap vector out of range.");
				continue;
			}
			wr_short (0x4e40 | imm.val);
			continue;
		}
		/* LINK */
		if (strncmp (pos, "link.w", 6) == 0) {
			pos += 6;
			check_whitespace();
			pos = get_reg (pos, &reg, F_AREG);
			check (',');
			pos = get_imm (pos, &imm, F_IMM | F_NOLABELS);
			check_end (pos);
			check_range (&imm.val, WORD);
			op.code = 0x4e50;
			op.type1.ea_reg = reg;
			wr_short (op.code);
			wr_short (imm.val);
			continue;
		}
		/* UNLK */
		if (strncmp (pos, "unlk", 4) == 0) {
			pos += 4;
			check_whitespace();
			pos = get_reg (pos, &reg, F_AREG);
			check_end (pos);
			op.code = 0x4e58;
			op.type1.ea_reg = reg;
			wr_short (op.code);
			continue;
		}
		/* MOVEQ */
		if (strncmp (pos, "moveq", 5) == 0){
			pos += 5;
			if (strncmp (pos, ".l", 2) == 0) pos += 2; /* gas poop */
			check_whitespace();
			pos = get_imm (pos, &imm, F_NOLABELS | F_IMM);
			check_range (&imm.val, BYTE);
			check (',');
			pos = get_ea (pos, &ea, LONG, F_DREG);
			check_end (pos);
			op.code = 0x7000;
			op.moveq.reg = ea.reg;
			op.moveq.data = imm.val;
			wr_short (op.code);
			continue;
		}
		/* MOVEA */
		if (strncmp (pos, "movea.", 6) == 0) {
			pos += 6;
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_ALL);
			check (',');
			pos = get_reg (pos, &i, F_AREG);
			check_end (pos);
is_movea:		
			if (size == BYTE) error ("Illegal size.");
			size = move_size[size];
			op.code = 0;
			op.move.size = size;
			op.move.dest_reg = i;
			op.move.dest_mode = 1;
			op.move.src_reg = ea.reg;
			op.move.src_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			continue;
		}
		/* MOVE */
		if (strncmp (pos, "move.", 5) == 0) {
			pos += 5;
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_ALL);
			check (',');
			pos = get_ea (pos, &ea2, size, F_AREG | F_DREG | F_IND | F_PRE | F_POST | F_INDEX | F_OFFSET | F_MEM_W | F_MEM_L);
			check_end (pos);
			
			if ((ea.mode == 7) && (ea.reg == 1) && (ea.imm.has_label)) {
				if (strcmp (ea.imm.label, "sr")==0) {
					/* move from sr */
					if (size != WORD) error ("Illegal size.");
					op.code = 0x40c0;
					op.type1.ea_reg = ea2.reg;
					op.type1.ea_mode = ea2.mode;
					wr_short (op.code);
					wr_ea (&ea2);
					continue;
				}
			}
			if (ea2.mode == 1) {
				i = ea2.reg;
				goto is_movea;
			}
			if ((ea2.mode == 7) && (ea2.reg == 1) && (ea2.imm.has_label)) {
				if (strcmp (ea2.imm.label, "sr")==0) {
					/* move to status reg */
					if (size != WORD) error ("Illegal size.");
					op.code = 0x46c0;
					op.type1.ea_reg = ea.reg;
					op.type1.ea_mode = ea.mode;
					wr_short (op.code);
					wr_ea (&ea);
					continue;
				}
			}
			size = move_size[size];
			op.code = 0;
			op.move.size = size;
			op.move.dest_reg = ea2.reg;
			op.move.dest_mode = ea2.mode;
			op.move.src_reg = ea.reg;
			op.move.src_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			wr_ea (&ea2);
			continue;
		}
		/* TST */
		if (strncmp (pos, "tst.", 4) == 0) {
			pos += 4;
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_NOIMM_NOPC_NOAREG);
			check_end (pos);
			op.code = 0x4a00;
			op.type1.size = size;
			op.type1.ea_reg = ea.reg;
			op.type1.ea_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			continue;
		}
		/* PEA */
		if (strncmp (pos, "pea", 3) == 0) {
			pos += 3;
			check_whitespace();
			pos = get_ea (pos, &ea, LONG, F_IND | F_OFFSET | F_INDEX | F_MEM_W | F_MEM_L | F_PC_OFFSET | F_PC_INDEX);
			check_end (pos);
			op.code = 0x4840;
			op.type2.ea_reg = ea.reg;
			op.type2.ea_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			continue;
		}
		/* LEA */
		if (strncmp (pos, "lea", 3) == 0) {
			pos += 3;
			check_whitespace();
			pos = get_ea (pos, &ea, LONG, F_IND | F_OFFSET | F_INDEX | F_MEM_W | F_MEM_L | F_PC_OFFSET | F_PC_INDEX);
			check (',');
			pos = get_reg (pos, &size, F_AREG);
			check_end (pos);
			op.code = 0x41e0;
			op.type2.ea_reg = ea.reg;
			op.type2.ea_mode = ea.mode;
			op.type2.reg = size-8;
			wr_short (op.code);
			wr_ea (&ea);
			continue;
		}

		/* TAS */
		if (strncmp (pos, "tas", 3) == 0) {
			pos += 3;
			check_whitespace ();
			pos = get_ea (pos, &ea, BYTE, F_NOIMM_NOPC_NOAREG);
			check_end (pos);
			op.code = 0x4ac0;
			op.type1.ea_reg = ea.reg;
			op.type1.ea_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			continue;
		}
		/* Scc */
		if (pos[0] == 's') {
			pos = rd_label (&pos[1], lab1);
			for (i=0; i<16; i++) {
				if (strncmp (lab1, DBcc_str[i], 2)==0) break;
			}
			if (i==16) goto err;
			while (isalpha (*pos)) pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, BYTE, F_NOIMM_NOPC_NOAREG);
			check_end (pos);
			op.code = 0x50c0;
			op.DBcc.cond = i;
			op.type1.ea_reg = ea.reg;
			op.type1.ea_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			continue;
		}
err:
		error ("Unknown opcode %s.", pos);
	}
	/* write text section length */
	size = get_bitpos () - BASE;
	set_bitpos (2);
	wr_int (size);
	/* empty reloc table for the moment */
	set_bitpos (size + BASE);
	wr_int (0);
	return size + BASE;
}

int asm_pass2 (int fixup_pos)
{
	struct Label *lab;
	struct Fixup *fix;
	int last_adr = BASE;
	int _first = 1;
	int dist;
	int num_relocs = 0;

	fix = fix_first;

	while (fix) {
		line_no = fix->line_no;
		lab = get_label (fix->label);
		if (!lab) {
			error ("Undefined label '%s'.", fix->label);
		}
		if (lab->type == L_CONST) {
			error ("Illegal absolute value.");
		}
		set_bitpos (fix->adr);
		if (fix->size == BYTE) {
			dist = lab->val + fix->offset - fix->rel_to;
			if ((dist < -128) || (dist > 127)) {
				error ("Offset too big (%d).", dist);
			}
			wr_byte (dist);
		} else if (fix->size == WORD) {
			dist = lab->val + fix->offset - fix->rel_to;
			if ((dist < -32768) || (dist > 32767)) {
				error ("Offset too big (%d).", dist);
			}
			wr_short (dist);
		} else if (fix->size >= LONG) {
			num_relocs++;
			wr_int (lab->val + fix->offset);
			set_bitpos (fixup_pos);
			if (_first) {
				wr_int (fix->adr - BASE);
				last_adr = fix->adr;
				fixup_pos+=4;
				_first = 0;
			} else {
				dist = fix->adr - last_adr;
				while (dist > 254) {
					wr_byte (1);
					dist -= 254;
					fixup_pos++;
				}
				wr_byte (dist);
				last_adr = fix->adr;
				fixup_pos++;
			}
		}  else {error ("Unknown size in fixup tab.");}
		fix = fix->next;
	}
	set_bitpos (fixup_pos);
	if (_first) wr_int (0);
	else wr_byte (0);

	return num_relocs;
}

int main (int argc, char **argv)
{
	int size, num;
    const char *output_filename = "aout.prg";
	if (argc == 1) {
		printf ("Usage: ./as68k [--dump-labels] [-o output_name.prg] file.s\n");
		exit (EXIT_SUCCESS);
	}
	
	argv++;

	if (strcmp (*argv, "--dump-labels") == 0) {
		argv++;
		dump_labels = 1;
	}
    if (strcmp (*argv, "-o") == 0) {
        argv++;
        output_filename = argv[0];
        argv++;
    }

	if ((fin = fopen (*argv, "r"))==NULL) {
		printf ("Error. Cannot open %s.\n", *argv);
		exit (EXIT_FAILURE);
	}

	fout = fopen (output_filename, "w");

	printf ("Pass 1\n");
	size = asm_pass1 ();
	printf ("Pass 2\n");
	num = asm_pass2 (size);
	fseek (fout, 0, SEEK_END);
	size = ftell (fout);

	printf ("Built %s! %d bytes and %d relocations.\n", output_filename, size, num);
	return 0;
}

