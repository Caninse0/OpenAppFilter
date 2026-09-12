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

-- bytes occupied by the custom rule file (0 when it does not exist yet)
local function rules_file_bytes()
	local f = io.open(RULES_FILE, "r")
	if not f then
		return 0
	end
	local content = f:read("*a")
	f:close()
	return content and #content or 0
end

-- Hard ceiling for the automatically calculated rule limit.
--
-- oafd allocates calloc(limit, sizeof(custom_rule_entry_t)) (264 bytes per
-- entry) and turns every domain rule into two kernel features, so a value
-- taken straight from the free disk space (hundreds of thousands on a roomy
-- overlay) would make the allocation fail and silently stop loading the
-- whole rule set. Keep the displayed value, the stored value and what the
-- daemon is willing to load identical by clamping here.
local RULE_LIMIT_MAX = 2000

-- rule limit = (free disk bytes - custom rule file bytes) / 80
-- decimal units are used, so 1 MB = 1000 KB
local function calc_rule_limit()
	local stat = fs.statvfs("/etc/appfilter") or fs.statvfs("/etc") or fs.statvfs("/")
	if not stat then
		return 0
	end

	local bavail = stat.bavail or stat.bfree or 0
	local bsize = stat.bsize or stat.frsize or 0
	local free_bytes = bavail * bsize - rules_file_bytes()

	if free_bytes < 0 then
		free_bytes = 0
	end

	local limit = math.floor(free_bytes / 80)
	if limit > RULE_LIMIT_MAX then
		limit = RULE_LIMIT_MAX
	end

	return limit
end

local function push_rules()
	util.ubus("fwx", "common", { api = "reload_custom_rule", data = {} })
end

local m, s

m = Map("fwx", translate("Custom Rules"),
	translate("Custom rules follow the AdGuard Home syntax. They take effect immediately after saving, no feature library update is required. Custom rules are matched before the feature library, so they can override library rules."))

s = m:section(NamedSection, "global", "global", translate("General"))
s.anonymous = true
s.addremove = false

-- The rule limit is derived from the disk state, never entered by the user.
-- It is only written back to fwx.global.custom_rule_max_num from
-- Map.on_after_save() below, and only when it differs from the stored value.
local rule_limit = calc_rule_limit()
local stored_limit = tonumber(m.uci:get("fwx", "global", "custom_rule_max_num") or "")
local limit_changed = (stored_limit ~= rule_limit)

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

-- Keep this in sync with update_custom_rule_state() in fwx_custom_rule.c:
--   custom_rule_follow_appfilter = 1 -> custom rules start and stop together
--                                       with the app filter switch
--   custom_rule_follow_appfilter = 0 -> custom rules run on their own; the
--                                       kernel only suspends the library rules
--                                       while the app filter switch is off
function status.cfgvalue(self, section)
	local af_raw = read_proc("/proc/sys/fwx/appfilter_enable")
	local enabled = tonumber(m.uci:get("fwx", "global", "custom_rule_enable") or "1") or 1
	local follow_sw = tonumber(m.uci:get("fwx", "global", "custom_rule_follow_appfilter") or "0") or 0

	if enabled ~= 1 then
		return translate("Disabled")
	end

	-- /proc/sys/fwx is missing: the kernel module or the daemon is not up
	if not af_raw then
		return translate("Not Running")
	end

	local af_on = tonumber(af_raw) or 0

	if follow_sw == 1 then
		if af_on == 1 then
			return translate("Runs with App Filter")
		end
		return translate("Stopped with App Filter")
	end

	if af_on == 1 then
		return translate("Runs independently")
	end
	return translate("Custom rules only")
end

local max_num = s:option(DummyValue, "_custom_rule_max_num", translate("Rule Limit"))
max_num.rawhtml = true

function max_num.cfgvalue(self, section)
	local text = translate("Current limit") .. ": " .. tostring(rule_limit) .. " " ..
		translate("valid rules") .. ". " ..
		translate("It is calculated in this page as floor((available disk space bytes - custom rule file bytes) / 80); decimal units are used, so 1 MB = 1000 KB; the limit is capped at %d rules."):format(RULE_LIMIT_MAX)

	if limit_changed then
		text = text .. " " ..
			translate("The calculated rule limit has changed. Saving will also apply it, just like Save & Apply.")
	end

	return text
end

local rules = s:option(TextValue, "_rules", translate("Rules (AdGuard Home Syntax)"),
	translate("One rule per line:<br/>" ..
		"||example.org^ - block example.org and all subdomains; * is allowed inside the domain<br/>" ..
		"@@||sub.example.org^ - allow sub.example.org and all its subdomains<br/>" ..
		"/REGEX/ - block domains matching the regular expression<br/>" ..
		"! comment or # comment - comment line<br/>" ..
		"<br/>" ..
		"Notes: custom rules are matched before the feature library; allow (ignore) rules have higher priority than block rules; " ..
		"the rule must not contain # ; , [ or ]; the rule limit is calculated in this page as floor((available disk space bytes - custom rule file bytes) / 80), " ..
		"using decimal units where 1 MB = 1000 KB; the limit is capped at %d rules; changes take effect within a few seconds after saving."):format(RULE_LIMIT_MAX))
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
	-- the rule file is written during parsing, so the calc_rule_limit() call
	-- in on_after_save() already sees the new file size; request the reload
	-- here as well so an edited rule set is never left unloaded
	push_rules()
	return true
end

-- Called on every save, including the plain "Save" button of this map.
--
-- A plain "Save" only stages the values in the per-session UCI delta, which
-- oafd cannot see, so they are committed right away. When the calculated rule
-- limit differs from the stored one, the page also commits and applies it,
-- which makes the "Save" button behave like the regular "Save & Apply".
function m.on_after_save(self)
	local computed = calc_rule_limit()
	local stored = tonumber(self.uci:get("fwx", "global", "custom_rule_max_num") or "")
	local changed = (stored ~= computed)

	if changed then
		self.uci:set("fwx", "global", "custom_rule_max_num", tostring(computed))
		self.uci:save("fwx")
	end

	self.uci:commit("fwx")

	if changed then
		self.uci:apply()
	end

	push_rules()
end

return m
