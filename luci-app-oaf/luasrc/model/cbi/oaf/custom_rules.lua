-- Custom rules (AdGuard Home syntax) editor for OpenAppFilter
local cbi = require "luci.cbi"
local fs = require "nixio.fs"
local util = require "luci.util"

local RULES_FILE = "/etc/appfilter/custom_rules.txt"

local function read_proc(path)
	local f = io.open(path, "r")
	if not f then
		return nil
	end
	local v = f:read("*l")
	f:close()
	return v
end

local function calc_rule_limit()
	local stat = fs.statvfs("/etc/appfilter") or fs.statvfs("/etc") or fs.statvfs("/")
	if not stat then
		return 0
	end

	local bavail = stat.bavail or stat.bfree or 0
	local bsize = stat.bsize or stat.frsize or 0
	local available_bytes = bavail * bsize

	return math.floor(available_bytes / (80 * 1000))
end

local function push_rules()
	util.ubus("fwx", "common", { api = "reload_custom_rule", data = {} })
end

local m, s
local rule_limit = calc_rule_limit()

m = Map("fwx", translate("Custom Rules"),
	translate("Custom rules follow the AdGuard Home syntax. They take effect immediately after saving, no feature library update is required. Custom rules are matched before the feature library, so they can override library rules."))

s = m:section(NamedSection, "global", "global", translate("General"))
s.anonymous = true
s.addremove = false

local enable = s:option(Flag, "custom_rule_enable", translate("Enable Custom Rules"),
	translate("If disabled, all custom rules will not take effect."))
enable.rmempty = false

function enable.cfgvalue(self, section)
	local v = m.uci:get("fwx", "global", "custom_rule_enable")
	if v == nil then
		return "1"
	end
	return v
end

local follow = s:option(Flag, "custom_rule_follow_appfilter", translate("Follow App Filter Switch"),
	translate("If enabled, custom rules take effect only while the app filter switch is on. If disabled, custom rules keep running on their own when the app filter is switched off."))
follow.rmempty = false

function follow.cfgvalue(self, section)
	local v = m.uci:get("fwx", "global", "custom_rule_follow_appfilter")
	if v == nil then
		return "0"
	end
	return v
end

local status = s:option(DummyValue, "_custom_rule_status", translate("Current Status"))

function status.cfgvalue(self, section)
	local only = tonumber(read_proc("/proc/sys/fwx/custom_rule_only_mode") or "") or 0
	local af_on = tonumber(read_proc("/proc/sys/fwx/appfilter_enable") or "") or 0
	local enabled = tonumber(m.uci:get("fwx", "global", "custom_rule_enable") or "1") or 1
	local follow_sw = tonumber(m.uci:get("fwx", "global", "custom_rule_follow_appfilter") or "0") or 0

	if enabled ~= 1 then
		return translate("Disabled")
	end
	if only == 1 then
		return translate("Custom rules only")
	end
	if follow_sw == 1 and af_on ~= 1 then
		return translate("Disabled")
	end
	return translate("Runs with App Filter")
end

local max_num = s:option(DummyValue, "_custom_rule_max_num", translate("Rule Limit"))
max_num.rawhtml = true

function max_num.cfgvalue(self, section)
	m.uci:set("fwx", "global", "custom_rule_max_num", tostring(rule_limit))
	return translate("Current limit") .. ": " .. tostring(rule_limit) .. " " ..
		translate("valid rules") .. ". " ..
		translate("It is calculated in this page as floor(available disk space bytes / (80 * 1000)); decimal units are used, so 1 MB = 1000 KB.")
end

local rules = s:option(TextValue, "_rules", translate("Rules (AdGuard Home Syntax)"),
	translate("One rule per line:<br/>" ..
		"||example.org^ - block example.org and all subdomains; * is allowed inside the domain<br/>" ..
		"@@||sub.example.org^ - allow sub.example.org and all its subdomains<br/>" ..
		"/REGEX/ - block domains matching the regular expression<br/>" ..
		"! comment or # comment - comment line<br/>" ..
		"<br/>" ..
		"Notes: custom rules are matched before the feature library; allow (ignore) rules have higher priority than block rules; " ..
		"the rule must not contain # ; , [ or ]; the rule limit is calculated in this page as floor(available disk space bytes / (80 * 1000)), " ..
		"using decimal units where 1 MB = 1000 KB; changes take effect within a few seconds after saving."))
rules.rows = 22
rules.rmempty = false
rules.wrap = "off"

function rules.cfgvalue(self, section)
	local f = io.open(RULES_FILE, "r")
	if not f then
		return ""
	end
	local content = f:read("*a")
	f:close()
	return content
end

function rules.write(self, section, value)
	os.execute("mkdir -p /etc/appfilter")
	local f = io.open(RULES_FILE, "w")
	if f then
		f:write(value or "")
		f:close()
	end
	-- also request a reload here so the rules take effect even if the
	-- host LuCI version does not call Map.on_after_commit
	push_rules()
	return true
end

function m.on_after_commit(self)
	m.uci:set("fwx", "global", "custom_rule_max_num", tostring(rule_limit))
	m.uci:commit("fwx")
	push_rules()
end

return m
