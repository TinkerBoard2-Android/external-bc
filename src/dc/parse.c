/*
 * *****************************************************************************
 *
 * Copyright 2018 Gavin D. Howard
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * *****************************************************************************
 *
 * The parser for dc.
 *
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <status.h>
#include <parse.h>
#include <dc.h>
#include <program.h>
#include <vm.h>

#ifdef DC_ENABLED
BcStatus dc_parse_register(BcParse *p) {

	BcStatus s;
	char *name;

	if ((s = bc_lex_next(&p->l))) return s;
	if (p->l.t.t != BC_LEX_NAME) return BC_STATUS_PARSE_BAD_TOKEN;
	if (!(name = strdup(p->l.t.v.v))) return BC_STATUS_ALLOC_ERR;
	if ((s = bc_parse_pushName(p, name))) free(name);

	return s;
}

BcStatus dc_parse_string(BcParse *p) {

	BcStatus s = BC_STATUS_ALLOC_ERR;
	char *str, *name, b[DC_PARSE_BUF_LEN + 1];
	size_t idx, len = p->prog->strs.len;

	if (sprintf(b, "%0*zu", DC_PARSE_BUF_LEN, len) < 0) return BC_STATUS_IO_ERR;
	if (!(name = strdup(b))) return s;

	if (!(str = strdup(p->l.t.v.v))) goto str_err;
	if ((s = bc_parse_push(p, BC_INST_STR))) goto err;
	if ((s = bc_parse_pushIndex(p, len))) goto err;
	if ((s = bc_vec_push(&p->prog->strs, &str))) goto err;
	if ((s = bc_parse_addFunc(p, name, &idx))) return s;
	if ((s = bc_lex_next(&p->l))) return s;

	assert(idx == len + BC_PROG_REQ_FUNCS);

	return s;

err:
	free(str);
str_err:
	free(name);
	return s;
}

BcStatus dc_parse_mem(BcParse *p, uint8_t inst, bool name, bool store) {

	BcStatus s;

	if ((s = bc_parse_push(p, inst))) return s;
	if (name && (s = dc_parse_register(p))) return s;

	if (store) {
		if ((s = bc_parse_push(p, BC_INST_SWAP))) return s;
		if ((s = bc_parse_push(p, BC_INST_ASSIGN))) return s;
		if ((s = bc_parse_push(p, BC_INST_POP))) return s;
	}

	return bc_lex_next(&p->l);
}

BcStatus dc_parse_cond(BcParse *p, uint8_t inst) {

	BcStatus s;

	if ((s = bc_parse_push(p, inst))) return s;
	if ((s = bc_parse_push(p, BC_INST_EXEC_COND))) return s;
	if ((s = dc_parse_register(p))) return s;
	if ((s = bc_lex_next(&p->l))) return s;

	if (p->l.t.t == BC_LEX_ELSE) {
		if ((s = dc_parse_register(p))) return s;
		s = bc_lex_next(&p->l);
	}
	else s = bc_parse_push(p, BC_PARSE_STREND);

	return s;
}

BcStatus dc_parse_token(BcParse *p, BcLexType t, uint8_t flags) {

	BcStatus s = BC_STATUS_SUCCESS;
	BcInst prev;
	uint8_t inst;
	bool assign, get_token = false;

	switch (t) {

		case BC_LEX_OP_REL_EQ:
		case BC_LEX_OP_REL_LE:
		case BC_LEX_OP_REL_GE:
		case BC_LEX_OP_REL_NE:
		case BC_LEX_OP_REL_LT:
		case BC_LEX_OP_REL_GT:
		{
			s = dc_parse_cond(p, t - BC_LEX_OP_REL_EQ + BC_INST_REL_EQ);
			break;
		}

		case BC_LEX_SCOLON:
		case BC_LEX_COLON:
		{
			s = dc_parse_mem(p, BC_INST_ARRAY_ELEM, true, t == BC_LEX_COLON);
			break;
		}

		case BC_LEX_STR:
		{
			s = dc_parse_string(p);
			break;
		}

		case BC_LEX_NEG:
		case BC_LEX_NUMBER:
		{
			if (t == BC_LEX_NEG) {
				if ((s = bc_lex_next(&p->l))) return s;
				if (p->l.t.t != BC_LEX_NUMBER) return BC_STATUS_PARSE_BAD_TOKEN;
			}

			s = bc_parse_number(p, &prev, &p->nbraces);

			if (t == BC_LEX_NEG && !s) s = bc_parse_push(p, BC_INST_NEG);
			get_token = true;

			break;
		}

		case BC_LEX_KEY_READ:
		{
			if (flags & BC_PARSE_NOREAD) s = BC_STATUS_EXEC_REC_READ;
			else s = bc_parse_push(p, BC_INST_READ);
			get_token = true;
			break;
		}

		case BC_LEX_OP_ASSIGN:
		case BC_LEX_STORE_PUSH:
		{
			assign = t == BC_LEX_OP_ASSIGN;
			inst = assign ? BC_INST_VAR : BC_INST_PUSH_TO_VAR;
			s = dc_parse_mem(p, inst, true, assign);
			break;
		}

		case BC_LEX_LOAD:
		case BC_LEX_LOAD_POP:
		{
			inst = t == BC_LEX_LOAD_POP ? BC_INST_PUSH_VAR : BC_INST_LOAD;
			s = dc_parse_mem(p, inst, true, false);
			break;
		}

		case BC_LEX_STORE_IBASE:
		case BC_LEX_STORE_SCALE:
		case BC_LEX_STORE_OBASE:
		{
			inst = t - BC_LEX_STORE_IBASE + BC_INST_IBASE;
			s = dc_parse_mem(p, inst, false, true);
			break;
		}

#ifndef NDEBUG
		default:
		{
			assert(false);
			break;
		}
#endif // NDEBUG
	}

	if (!s && get_token) s = bc_lex_next(&p->l);

	return s;
}

BcStatus dc_parse_expr(BcParse *p, uint8_t flags) {

	BcStatus s = BC_STATUS_SUCCESS;
	BcInst inst;
	BcLexType t;

	if (flags & BC_PARSE_NOCALL) p->nbraces = p->prog->results.len;

	while (!s && (t = p->l.t.t) != BC_LEX_EOF) {
		if ((inst = dc_parse_insts[t]) != BC_INST_INVALID) {
			if ((s = bc_parse_push(p, inst))) return s;
			if ((s = bc_lex_next(&p->l))) return s;
		}
		else if ((s = dc_parse_token(p, t, flags))) return s;
	}

	if (!s && p->l.t.t == BC_LEX_EOF && (flags & BC_PARSE_NOCALL))
		s = bc_parse_push(p, BC_INST_POP_EXEC);

	return s;
}

BcStatus dc_parse_parse(BcParse *p) {

	BcStatus s;

	assert(p);

	if (p->l.t.t == BC_LEX_EOF) s = BC_STATUS_LEX_EOF;
	else s = dc_parse_expr(p, 0);

	if (s || bcg.signe) s = bc_parse_reset(p, s);

	return s;
}

BcStatus dc_parse_init(BcParse *p, BcProgram *prog, size_t func) {
	assert(p && prog);
	return bc_parse_create(p, prog, func, dc_parse_parse, dc_lex_token);
}
#endif // DC_ENABLED
