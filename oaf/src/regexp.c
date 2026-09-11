#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/string.h>


typedef enum{CHAR, DOT, BEGIN, END, STAR, PLUS, QUES, LIST, TYPENUM}TYPE;

#define MAX_REGEXP_EXPAND_LEN 256
#define MAX_REGEXP_EXPAND_DEPTH 8

typedef struct RE{
	TYPE type;
	int ch;
	char *ccl;
	int nccl;
	struct RE *next;
}RE;

int match_longest = 0;
char *match_first = NULL;


static void * getmem(size_t size)
{
	void *tmp;
	if((tmp = kmalloc(size, GFP_ATOMIC))==NULL)
	{
		printk("malloc failed");
		return NULL;
	}
	return tmp;
}

static size_t creat_list(char *str, int start, int end)
{
	size_t cnt = end - start + 1;
	for(; start <= end ;start++)
		*str++ = start;
	return (cnt > 0)?cnt:0;
}

static int in_list(char ch, RE *regexp)
{
	char *str = regexp->ccl;
	if(regexp->type != LIST)
		return 0;
	for(; *str && ch != *str; str++)
		;
	return (*str != '\0') ^ regexp->nccl;
}

static int regexp_has_group_alt(char *reg)
{
	char *p = reg;
	int escaped = 0;

	for (; *p; p++)
	{
		if (escaped)
		{
			escaped = 0;
			continue;
		}
		if (*p == '\\')
		{
			escaped = 1;
			continue;
		}
		if (*p == '(' || *p == ')' || *p == '|')
			return 1;
	}
	return 0;
}

static int append_part(char *dst, int dst_len, int *pos, const char *src, int len)
{
	if (*pos + len >= dst_len)
		return -1;
	memcpy(dst + *pos, src, len);
	*pos += len;
	dst[*pos] = '\0';
	return 0;
}

static int regexp_match_base(char *reg, char *text);
static int regexp_match_expand(char *reg, char *text, int depth);

static int match_top_level_alt(char *reg, char *text, int depth)
{
	char *p = reg;
	char *begin = reg;
	int escaped = 0;
	int group_depth = 0;
	int saw_top_alt = 0;

	for (; ; p++)
	{
		if (escaped)
		{
			escaped = 0;
		}
		else if (*p == '\\')
		{
			escaped = 1;
		}
		else if (*p == '(')
		{
			group_depth++;
		}
		else if (*p == ')' && group_depth > 0)
		{
			group_depth--;
		}
		else if ((*p == '|' && group_depth == 0) || *p == '\0')
		{
			char alt[MAX_REGEXP_EXPAND_LEN] = {0};
			int len = p - begin;

			if (*p == '|' && group_depth == 0)
				saw_top_alt = 1;
			if (*p == '\0' && !saw_top_alt)
				return -2;
			if (len <= 0 || len >= MAX_REGEXP_EXPAND_LEN)
				return -1;
			memcpy(alt, begin, len);
			alt[len] = '\0';
			if (regexp_match_expand(alt, text, depth + 1) == 1)
				return 1;
			begin = p + 1;
		}
		if (*p == '\0')
			break;
	}
	return 0;
}

static int find_alt_group(char *reg, char **open, char **close)
{
	char *p = reg;
	char *stack[MAX_REGEXP_EXPAND_DEPTH] = {0};
	int has_alt[MAX_REGEXP_EXPAND_DEPTH] = {0};
	int escaped = 0;
	int depth = 0;

	for (; *p; p++)
	{
		if (escaped)
		{
			escaped = 0;
			continue;
		}
		if (*p == '\\')
		{
			escaped = 1;
			continue;
		}
		if (*p == '(')
		{
			if (depth >= MAX_REGEXP_EXPAND_DEPTH)
				return -1;
			stack[depth] = p;
			has_alt[depth] = 0;
			depth++;
			continue;
		}
		if (*p == '|')
		{
			if (depth > 0)
				has_alt[depth - 1] = 1;
			continue;
		}
		if (*p == ')')
		{
			if (depth <= 0)
				return -1;
			depth--;
			if (has_alt[depth])
			{
				*open = stack[depth];
				*close = p;
				return 1;
			}
		}
	}
	if (depth != 0)
		return -1;
	return 0;
}

static int strip_plain_groups(char *dst, int dst_len, char *reg)
{
	char *p = reg;
	int pos = 0;
	int escaped = 0;
	int depth = 0;

	for (; *p; p++)
	{
		if (!escaped && *p == '\\')
		{
			escaped = 1;
			if (append_part(dst, dst_len, &pos, p, 1) < 0)
				return -1;
			continue;
		}
		if (!escaped && *p == '(')
		{
			depth++;
			continue;
		}
		if (!escaped && *p == ')')
		{
			if (depth <= 0)
				return -1;
			depth--;
			continue;
		}
		escaped = 0;
		if (append_part(dst, dst_len, &pos, p, 1) < 0)
			return -1;
	}
	return depth == 0 ? 0 : -1;
}

static int match_group_alt(char *reg, char *text, int depth, char *open, char *close)
{
	char *begin = open + 1;
	char *p = begin;
	int escaped = 0;
	int group_depth = 0;
	int prefix_len = open - reg;
	int suffix_len = strlen(close + 1);

	for (; ; p++)
	{
		if (escaped)
		{
			escaped = 0;
		}
		else if (*p == '\\')
		{
			escaped = 1;
		}
		else if (*p == '(')
		{
			group_depth++;
		}
		else if (*p == ')' && group_depth > 0)
		{
			group_depth--;
		}
		else if ((*p == '|' && group_depth == 0) || p == close)
		{
			char expanded[MAX_REGEXP_EXPAND_LEN] = {0};
			int pos = 0;
			int alt_len = p - begin;

			if (append_part(expanded, sizeof(expanded), &pos, reg, prefix_len) < 0 ||
				append_part(expanded, sizeof(expanded), &pos, begin, alt_len) < 0 ||
				append_part(expanded, sizeof(expanded), &pos, close + 1, suffix_len) < 0)
				return -1;
			if (regexp_match_expand(expanded, text, depth + 1) == 1)
				return 1;
			begin = p + 1;
		}
		if (p == close)
			break;
	}
	return 0;
}

static void regexp_free(RE *regexp)
{
	RE *tmp;
	for(; regexp; regexp = tmp)
	{
		tmp = regexp->next;
		if (regexp->type == LIST && regexp->ccl)
			kfree(regexp->ccl);
		kfree(regexp);
	}
}

static RE* compile(char *regexp)
{
	RE head, *tail, *tmp;
	char *pstr;
	int err_flag = 0;

	for(tail = &head; *regexp != '\0' && err_flag == 0; regexp++)
	{
		tmp = getmem(sizeof(RE));
		if (!tmp)
		{
			err_flag = 1;
			break;
		}
		memset(tmp, 0, sizeof(RE));
		switch(*regexp){
			case '\\':
				regexp++;
				if(*regexp == 'd')
				{
					tmp->type = LIST;
					tmp->nccl = 0;
					tmp->ccl = getmem(11);
					if (!tmp->ccl)
					{
						err_flag = 1;
						break;
					}
					creat_list(tmp->ccl, '0','9');
					tmp->ccl[10] = '\0';
				}else if(*regexp == 'D')
				{
					tmp->type = LIST;
					tmp->nccl = 1;
					tmp->ccl = getmem(11);
					if (!tmp->ccl)
					{
						err_flag = 1;
						break;
					}
					creat_list(tmp->ccl, '0','9');
					tmp->ccl[10] = '\0';
				}else
				{
					tmp->type = CHAR;
					tmp->ch = *regexp;
				}
				break;
			case '.':
				tmp->type = DOT;
				break;
			case '^':
				tmp->type = BEGIN;
				tmp->ch = '^';
				break;
			case '$':
				tmp->type = END;
				tmp->ch = '$';
				break;
			case '*':
				tmp->type = STAR;
				break;
			case '+':
				tmp->type = PLUS;
				break;
			case '?':
				tmp->type = QUES;
				break;
			case '[':
				pstr = tmp->ccl = getmem(256);
				if (!tmp->ccl)
				{
					err_flag = 1;
					break;
				}
				tmp->nccl = 0;
				if(*++regexp == '^')
				{
					tmp->nccl = 1;
					regexp++;
				}
				while(*regexp != '\0' && *regexp != ']')
				{
					if(*regexp != '-')
					{
						*pstr++ = *regexp++;
						continue;
					}
					if(pstr == tmp->ccl || *(regexp + 1) == ']')
					{
						err_flag = 1;
						break;
					}
					pstr += creat_list(pstr, *(regexp - 1) + 1, *(regexp + 1));
					regexp += 2;
				}
				*pstr = '\0';
				if(*regexp == '\0')
					err_flag = 1;
				tmp->type = LIST;
				break;
			default:
				tmp->type = CHAR;
				tmp->ch = *regexp;
		}

		tail->next = tmp;
		tail = tmp;
	}

	tail->next = NULL;
	if(err_flag)
	{
		regexp_free(head.next);
		return NULL;
	}
	return head.next;
}

#define MATCH_ONE(reg, text) \
   	(reg->type == DOT || in_list(*text, reg) || *text == reg->ch)
#define MATCH_ONE_P(reg, text) \
   	(in_list(*text++, reg) || *(text - 1) == reg->ch || reg->type == DOT)

static int matchhere(RE *regexp, char *text);

static int matchstar(RE *cur, RE *regexp, char *text)
{
	do{
		if(matchhere(regexp, text))
			return 1;
	}while(*text != '\0' && MATCH_ONE_P(cur, text));
	return 0;
}

static int matchstar_l(RE *cur, RE *regexp, char *text)
{
	char *t;
	for(t = text; *t != '\0' && MATCH_ONE(cur, t); t++)
		;
	do{
		if(matchhere(regexp, t))
			return 1;
	}while(t-- > text);
	return 0;
}

static int matchplus(RE *cur, RE *regexp, char *text)
{
	while(*text != '\0' && MATCH_ONE_P(cur, text))
	{
		if(matchhere(regexp, text))
			return 1;
	}
	return 0;
}

static int matchplus_l(RE *cur, RE *regexp, char *text)
{
	char *t;
	for(t = text; *t != '\0' && MATCH_ONE(cur, t); t++)
		;
	for(; t > text; t--)
	{
		if(matchhere(regexp, t))
			return 1;
	}
	return 0;
}

static int matchques(RE *cur, RE *regexp, char *text)
{
	int cnt = 1;
	char *t = text;
	if(*t != '\0' && cnt-- && MATCH_ONE(cur, t))
		t++;
	do{
		if(matchhere(regexp, t))
			return 1;
	}while(t-- > text);
	return 0;
}

static int (*matchfun[TYPENUM][2])(RE *, RE *, char *) = {
	[STAR] = { matchstar, matchstar_l },
	[PLUS] = { matchplus, matchplus_l },
	[QUES] = { matchques, matchques },
};

static int matchhere(RE *regexp, char *text)
{
	if(regexp == NULL)
		return 1;
	if(regexp->type == END && regexp->next == NULL)
		return *text == '\0';
	if(regexp->next && matchfun[regexp->next->type][match_longest])
		return matchfun[regexp->next->type][match_longest](regexp, regexp->next->next, text);

	if(*text != '\0' && MATCH_ONE(regexp, text))
		return matchhere(regexp->next, text + 1);
	return 0;
}

/* 
 * return value:
 *		-1		error
 *		0		not match
 *		1		matched
 */
static int regexp_match_base(char *reg, char *text)
{
	int ret;
	RE *regexp = compile(reg);
	if(regexp == NULL)
		return -1;

	if(regexp->type == BEGIN)
	{
		ret = matchhere(regexp->next, text);
		goto out;
	}

	do{
		if ((ret = matchhere(regexp, text)))
		{
			goto out;
		}
	}while(*text++ != '\0');

out:
	regexp_free(regexp);
	return ret;
}

static int regexp_match_expand(char *reg, char *text, int depth)
{
	char normalized[MAX_REGEXP_EXPAND_LEN] = {0};
	char *open = NULL;
	char *close = NULL;
	int ret;
	int alt_ret;

	if (depth > MAX_REGEXP_EXPAND_DEPTH)
		return -1;
	if (!regexp_has_group_alt(reg))
		return regexp_match_base(reg, text);
	if (strchr(reg, '|'))
	{
		alt_ret = match_top_level_alt(reg, text, depth);
		if (alt_ret != -2)
			return alt_ret;
	}

	ret = find_alt_group(reg, &open, &close);
	if (ret < 0)
		return -1;
	if (ret > 0)
		return match_group_alt(reg, text, depth, open, close);

	if (strip_plain_groups(normalized, sizeof(normalized), reg) < 0)
		return -1;
	return regexp_match_base(normalized, text);
}

int regexp_match(char *reg, char *text)
{
	if (!reg || !text)
		return -1;
	return regexp_match_expand(reg, text, 0);
}
