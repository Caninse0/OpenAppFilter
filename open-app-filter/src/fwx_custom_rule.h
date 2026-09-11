// SPDX-License-Identifier: GPL-2.0-or-later
/* 
 * Copyright(c) 2026 destan19(TT) <www.fanchmwrt.com>  
 */
#ifndef __FWX_CUSTOM_RULE_H__
#define __FWX_CUSTOM_RULE_H__

/* custom rules (AdGuard Home syntax) file */
#define CUSTOM_RULES_FILE "/etc/appfilter/custom_rules.txt"
#define MAX_CUSTOM_RULE_LINE_LEN 256

/* must match AF_CUSTOM_RULE_APPID_BASE in the oaf kernel module */
#define CUSTOM_RULE_APPID_BASE 30001

/* UCI keys, package fwx / section global.
 *
 * custom_rule_follow_appfilter:
 *   0 - custom rules run on their own. When the app filter switch is off,
 *       library based rules are suspended but custom rules keep working
 *       (kernel enters "custom rule only" mode).
 *   1 - custom rules follow the app filter switch and stop together with it.
 */
#define CUSTOM_RULE_ENABLE_KEY "fwx.global.custom_rule_enable"
#define CUSTOM_RULE_FOLLOW_KEY "fwx.global.custom_rule_follow_appfilter"
#define CUSTOM_RULE_MAX_NUM_KEY "fwx.global.custom_rule_max_num"

/* load custom rules from file and push them to the kernel via netlink */
int load_custom_rules(void);

/* returns whether custom rules are enabled */
int custom_rule_enabled(void);

/* returns whether custom rules follow the app filter switch */
int custom_rule_follow_appfilter(void);

/* re-evaluate the custom rule state (time window / appfilter switch).
 * returns 1 when the state changed and a feature reload is required. */
int update_custom_rule_state(void);

/* request a custom rule reload on the next timer tick */
void fwx_custom_rule_request_reload(void);

#endif
