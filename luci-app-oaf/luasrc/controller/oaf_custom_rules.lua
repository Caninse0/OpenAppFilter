module("luci.controller.oaf_custom_rules", package.seeall)

function index()
	local fs = require "nixio.fs"
	if not fs.access("/etc/config/fwx") then
		return
	end

	entry({"admin", "services", "oaf", "custom_rules"}, cbi("oaf/custom_rules", {hideapplybtn=true, hideresetbtn=true}), _("Custom Rules"), 28).leaf = true
end
