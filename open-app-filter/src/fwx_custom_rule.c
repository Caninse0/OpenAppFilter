// SPDX-License-Identifier: GPL-2.0-or-later
/* 
 * Copyright(c) 2026 destan19(TT) <www.fanchmwrt.com>  
 *
 * Custom rules (AdGuard Home syntax subset).
 *
 * Every custom rule is turned into a kernel feature string and pushed over
 * netlink, so it is matched by exactly the same DPI path as the built in
 * application feature library. All custom rules share a single appid
 * (CUSTOM_RULE_APPID_BASE) because the kernel only needs to know
 * "this flow matched a custom rule", not which rule matched.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <uci.h>
#include "fwx_custom_rule.h"
#include "fwx.h"
#include "fwx_netlink.h"
#include "fwx_uci.h"
#include "fwx_utils.h"

/* fwx_nl_add_feature() and g_feature_update live in main.c */
extern int fwx_nl_add_feature(char *feature);
extern int g_feature_update;

/* The kernel stores host_url in a MAX_HOST_URL_LEN (128) buffer, reserve 4 bytes. */
#define MAX_CUSTOM_REGEX_LEN 124
#define DEFAULT_CUSTOM_RULE_NUM 500
#define CUSTOM_RULE_APP_NAME "custom_rules"

/* The kernel regexp engine only takes the regexp path for a host_url that
 * starts with '^', contains '*' and ends with '$' (af_is_regex_host_pattern()).
 * Everything else is compiled into the Aho-Corasick automaton as a literal
 * sub string, which would never match a domain. Every pattern we generate
 * therefore has to stay inside that shape. */
#define CUSTOM_REGEX_PREFIX "^.*"
#define CUSTOM_REGEX_SUFFIX ".*$"

#define APPFILTER_ENABLE_PROC "/proc/sys/fwx/appfilter_enable"

enum {
    RULE_SKIP,
    RULE_DOMAIN_ALL, /* ||domain^ : domain and all subdomains, '*' allowed */
    RULE_REGEX,      /* /REGEX/ */
};

typedef struct {
    int type;
    int ignore;
    char pattern[MAX_CUSTOM_RULE_LINE_LEN];
} custom_rule_entry_t;

/* Whether custom rules should currently be pushed to the kernel.
 * Only consulted when custom_rule_follow_appfilter is set. */
int g_custom_rule_active = 1;

/* Last value written to /proc/sys/fwx/custom_rule_only_mode, -1 = unknown. */
static int g_custom_rule_only_mode_state = -1;

/* Last seen mtime of the rules file, so an edit is picked up even when the
 * LuCI side fails to request a reload. */
static long g_rule_file_sec = -1;
static long g_rule_file_nsec = -1;

static int custom_rule_file_changed(void)
{
    struct stat st;

    if (stat(CUSTOM_RULES_FILE, &st) != 0) {
        if (g_rule_file_sec < 0)
            return 0;
        g_rule_file_sec = -1;
        g_rule_file_nsec = -1;
        return 1;
    }
    if (st.st_mtim.tv_sec != g_rule_file_sec ||
        st.st_mtim.tv_nsec != g_rule_file_nsec) {
        int first_seen = (g_rule_file_sec < 0);

        g_rule_file_sec = st.st_mtim.tv_sec;
        g_rule_file_nsec = st.st_mtim.tv_nsec;
        /* the first observation only records the baseline */
        return first_seen ? 0 : 1;
    }
    return 0;
}

int custom_rule_enabled(void)
{
    int enable = 1;
    struct uci_context *ctx = uci_alloc_context();

    if (ctx) {
        int v = fwx_uci_get_int_value(ctx, (char *)CUSTOM_RULE_ENABLE_KEY);
        if (v >= 0)
            enable = v;
        uci_free_context(ctx);
    }
    return enable == 1;
}

int custom_rule_follow_appfilter(void)
{
    int enable = 0;
    struct uci_context *ctx = uci_alloc_context();

    if (ctx) {
        int v = fwx_uci_get_int_value(ctx, (char *)CUSTOM_RULE_FOLLOW_KEY);
        if (v >= 0)
            enable = v;
        uci_free_context(ctx);
    }
    return enable == 1;
}

static int custom_rule_max_num(void)
{
    int max_num = DEFAULT_CUSTOM_RULE_NUM;
    struct uci_context *ctx = uci_alloc_context();

    if (ctx) {
        int v = fwx_uci_get_int_value(ctx, (char *)CUSTOM_RULE_MAX_NUM_KEY);
        if (v > 0)
            max_num = v;
        uci_free_context(ctx);
    }
    return max_num;
}

/* the kernel lets the appfilter switch decide whether library rules run.
 * rule_manager owns that switch, so read it back instead of recomputing
 * the time window here. */
static int read_appfilter_enable(void)
{
    char buf[16] = {0};

    if (af_read_file_value(APPFILTER_ENABLE_PROC, buf, sizeof(buf)) != 0)
        return 1;
    return atoi(buf) == 1;
}

void fwx_custom_rule_request_reload(void)
{
    g_feature_update = 1;
}

int update_custom_rule_state(void)
{
    int appfilter_on = read_appfilter_enable();
    int custom_on = custom_rule_enabled();
    int follow_switch = custom_rule_follow_appfilter();
    int only_mode = 0;
    int active = 1;
    int changed = 0;

    /* pick up rule file edits even when no reload was requested */
    if (custom_rule_file_changed()) {
        LOG_WARN("custom rule file changed, reload required\n");
        changed = 1;
    }

    if (!custom_on) {
        only_mode = 0;
        active = 0;
    } else if (follow_switch) {
        /* custom rules stop together with the app filter switch */
        only_mode = 0;
        active = appfilter_on;
    } else {
        /* custom rules run on their own, library rules stay suspended */
        only_mode = appfilter_on ? 0 : 1;
        active = 1;
    }

    if (only_mode != g_custom_rule_only_mode_state) {
        update_fwx_proc_u32_value("custom_rule_only_mode", only_mode);
        g_custom_rule_only_mode_state = only_mode;
        LOG_WARN("custom rule only mode -> %d\n", only_mode);
    }

    if (active != g_custom_rule_active) {
        LOG_WARN("custom rule active state changed: %d -> %d\n",
                 g_custom_rule_active, active);
        g_custom_rule_active = active;
        changed = 1;
    }
    return changed;
}

static int is_escaped(const char *start, const char *p)
{
    int count = 0;

    while (p > start && p[-1] == '\\') {
        count++;
        p--;
    }
    return (count & 1);
}

static int append_str(char *dst, int dst_len, int *pos, const char *src)
{
    int len = strlen(src);

    if (*pos + len >= dst_len)
        return -1;
    memcpy(dst + *pos, src, len);
    *pos += len;
    dst[*pos] = '\0';
    return 0;
}

/* convert an adguard domain to a kernel regexp: '.' -> '\.', '*' -> '.*' */
static int escape_domain(char *dst, int dst_len, const char *domain)
{
    int len = 0;
    const char *p = domain;

    while (*p && len < dst_len - 1) {
        if (*p == '.') {
            if (len + 2 >= dst_len)
                break;
            dst[len++] = '\\';
            dst[len++] = '.';
        } else if (*p == '*') {
            if (len + 2 >= dst_len)
                break;
            dst[len++] = '.';
            dst[len++] = '*';
        } else {
            dst[len++] = *p;
        }
        p++;
    }
    dst[len] = '\0';
    return len;
}

/* strip adguard modifiers ('^...', '$...') and trailing blanks from a domain */
static void strip_domain_tail(char *domain)
{
    char *p = domain;

    while (*p) {
        if (*p == '^' || *p == '$' || *p == ' ' || *p == '\t' || *p == '\r')
            break;
        p++;
    }
    *p = '\0';
}

static int custom_rule_char_allowed(const char *pattern)
{
    static const char invalid[] = "#;,[";
    int i;

    for (i = 0; invalid[i]; i++) {
        if (strchr(pattern, invalid[i]))
            return 0;
    }
    /* ']' breaks the kernel feature line scanner as well */
    if (strchr(pattern, ']'))
        return 0;
    return 1;
}

/* build the kernel feature line and send it:
 *   block: <appid>~custom_rules:[tcp;;;REGEX;;;;0]
 *   allow: <appid>~custom_rules:[tcp;;;REGEX;;;;1]
 *
 * field layout, must match the kernel parser:
 *   proto;src_port;dst_port;host_url;request_url;dict;search_str;ignore
 */
static int add_custom_rule_feature(int appid, int ignore, const char *regex)
{
    char feature_buf[MAX_FEATURE_LINE_LEN] = {0};
    int ret;

    if (!regex || !regex[0])
        return -1;
    if (strlen(regex) > MAX_CUSTOM_REGEX_LEN) {
        LOG_WARN("custom rule regex too long, skip: %s\n", regex);
        return -1;
    }
    if (!custom_rule_char_allowed(regex)) {
        LOG_WARN("custom rule contains invalid char(# ; , [ ]), skip: %s\n", regex);
        return -1;
    }
    snprintf(feature_buf, sizeof(feature_buf), "%d~%s:[tcp;;;%s;;;;%d]",
             appid, CUSTOM_RULE_APP_NAME, regex, ignore);
    ret = fwx_nl_add_feature(feature_buf);
    if (ret < 0)
        LOG_ERROR("send custom rule feature failed: %s\n", feature_buf);
    else
        LOG_DEBUG("add custom rule: %s\n", feature_buf);
    return ret;
}

/* re-wrap a user supplied regexp so the kernel takes the regexp path:
 *   /ads\d+/        -> ^.*ads\d+.*$
 *   /^track\.x$/    -> ^.*track\.x.*$
 */
static int normalize_user_regex(char *dst, int dst_len, const char *src)
{
    const char *begin = src;
    const char *end = src + strlen(src);
    int pos = 0;

    while (begin < end && isspace((unsigned char)*begin))
        begin++;
    while (end > begin && isspace((unsigned char)end[-1]))
        end--;

    while (begin < end && *begin == '^')
        begin++;
    while (end > begin && end[-1] == '$' && !is_escaped(src, end - 1))
        end--;

    if (end <= begin)
        return -1;
    if (append_str(dst, dst_len, &pos, CUSTOM_REGEX_PREFIX) < 0)
        return -1;
    if (pos + (int)(end - begin) >= dst_len)
        return -1;
    memcpy(dst + pos, begin, end - begin);
    pos += (end - begin);
    dst[pos] = '\0';
    if (append_str(dst, dst_len, &pos, CUSTOM_REGEX_SUFFIX) < 0)
        return -1;
    return 0;
}

/* parse one adguard rule line. returns the rule type, and fills the ignore
 * flag plus the pattern (domain or regexp body).
 * supported syntax:
 *   ||example.org^
 *   @@||sub.example.org^
 *   /REGEX/
 *   ! comment, # comment, [Adblock Plus 2.0]
 */
static int parse_rule_line(char *line, int *ignore, char *pattern, int pattern_len)
{
    char *p = line;
    int type = RULE_SKIP;

    str_trim(line);
    if (!line[0] || line[0] == '!' || line[0] == '#')
        return RULE_SKIP;
    if (line[0] == '[')
        return RULE_SKIP;

    *ignore = 0;
    if (strncmp(p, "@@", 2) == 0) {
        *ignore = 1;
        p += 2;
        while (*p && isspace((unsigned char)*p))
            p++;
    }

    if (strncmp(p, "||", 2) == 0) {
        p += 2;
        type = RULE_DOMAIN_ALL;
    } else if (*p == '/') {
        char *end;

        if (*ignore)
            return RULE_SKIP;
        end = strrchr(p + 1, '/');
        if (end && end > p + 1) {
            *end = '\0';
            p++;
            type = RULE_REGEX;
        } else {
            return RULE_SKIP;
        }
    } else {
        return RULE_SKIP;
    }

    snprintf(pattern, pattern_len, "%s", p);
    if (type != RULE_REGEX)
        strip_domain_tail(pattern);

    if (!pattern[0])
        return RULE_SKIP;
    return type;
}

/* push one domain rule as two regexps:
 *   ^esc:?\d*$      matches the base domain, with an optional port
 *   ^.+\.esc:?\d*$  matches subdomains (at least one label before the dot)
 * both start with '^', contain '*' and end with '$', so the kernel treats
 * them as regexps and not as literal sub strings. */
static void add_domain_rule(int appid, int ignore, const char *domain)
{
    char esc[MAX_CUSTOM_REGEX_LEN] = {0};
    char reg[MAX_CUSTOM_REGEX_LEN] = {0};
    int ret;

    escape_domain(esc, sizeof(esc), domain);
    if (!esc[0])
        return;

    ret = snprintf(reg, sizeof(reg), "^%s:?\\d*$", esc);
    if (ret < (int)sizeof(reg)) {
        add_custom_rule_feature(appid, ignore, reg);
    } else {
        LOG_WARN("custom rule domain too long, skip: %s\n", domain);
        return;
    }
    ret = snprintf(reg, sizeof(reg), "^.+\\.%s:?\\d*$", esc);
    if (ret < (int)sizeof(reg))
        add_custom_rule_feature(appid, ignore, reg);
}

/* Load custom rules from CUSTOM_RULES_FILE and push them to the kernel.
 *
 * The kernel feature list is a list_add(head) list and matching walks it from
 * the head, so the last netlink message is matched first. Each group is
 * therefore sent in reverse file order:
 *
 *   block rules first, allow rules second
 *
 * which yields the match order
 *
 *   allow rules in file order, then block rules in file order
 *
 * so an exception such as @@||sub.example.org^ wins over ||example.org^
 * while the order inside one group stays stable.
 */
int load_custom_rules(void)
{
    FILE *fp = NULL;
    char line[MAX_CUSTOM_RULE_LINE_LEN] = {0};
    custom_rule_entry_t *rules = NULL;
    int max_rule_num = custom_rule_max_num();
    int rule_count = 0;
    int i;
    int appid = CUSTOM_RULE_APPID_BASE;

    if (!custom_rule_enabled()) {
        LOG_WARN("custom rule is disabled, skip load\n");
        return 0;
    }
    if (custom_rule_follow_appfilter() && !g_custom_rule_active) {
        LOG_WARN("custom rule follows app filter switch which is off, skip load\n");
        return 0;
    }

    fp = fopen(CUSTOM_RULES_FILE, "r");
    if (!fp) {
        LOG_DEBUG("custom rules file %s not found\n", CUSTOM_RULES_FILE);
        return 0;
    }
    if (max_rule_num <= 0) {
        LOG_WARN("custom rule limit is %d, skip load\n", max_rule_num);
        fclose(fp);
        return 0;
    }
    rules = calloc(max_rule_num, sizeof(custom_rule_entry_t));
    if (!rules) {
        LOG_ERROR("malloc custom rules failed, max_rule_num=%d\n", max_rule_num);
        fclose(fp);
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        int ignore = 0;
        int type;

        if (rule_count >= max_rule_num) {
            LOG_WARN("custom rule count exhausted(%d), ignore rest rules\n",
                     max_rule_num);
            break;
        }
        type = parse_rule_line(line, &ignore,
                               rules[rule_count].pattern,
                               sizeof(rules[rule_count].pattern));
        if (type == RULE_SKIP)
            continue;
        rules[rule_count].type = type;
        rules[rule_count].ignore = ignore;
        rule_count++;
    }

    for (i = rule_count - 1; i >= 0; i--) {
        if (rules[i].ignore != 0)
            continue;
        if (rules[i].type == RULE_REGEX) {
            char reg[MAX_CUSTOM_REGEX_LEN] = {0};

            if (normalize_user_regex(reg, sizeof(reg), rules[i].pattern) == 0)
                add_custom_rule_feature(appid, 0, reg);
        } else {
            add_domain_rule(appid, 0, rules[i].pattern);
        }
    }

    for (i = rule_count - 1; i >= 0; i--) {
        if (rules[i].ignore != 1)
            continue;
        if (rules[i].type == RULE_REGEX) {
            char reg[MAX_CUSTOM_REGEX_LEN] = {0};

            if (normalize_user_regex(reg, sizeof(reg), rules[i].pattern) == 0)
                add_custom_rule_feature(appid, 1, reg);
        } else {
            add_domain_rule(appid, 1, rules[i].pattern);
        }
    }

    LOG_WARN("load %d custom rules to kernel\n", rule_count);

    free(rules);
    fclose(fp);
    return 0;
}
