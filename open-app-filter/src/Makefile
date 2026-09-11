OBJS := \
    fwx_user.o \
    fwx_netlink.o \
    fwx_ubus.o \
    fwx_stat.o \
    fwx_config.o \
    fwx_feature.o \
    fwx_feature_online.o \
    fwx_custom_feature.o \
    fwx_custom_rule.o \
    fwx_utils.o \
    fwx_app_filter.o \
    fwx_mac_filter.o \
    fwx_record.o \
    fwx_common.o \
    fwx_system.o \
    fwx_network.o \
    fwx_firewall.o \
    fwx_wireless.o \
    fwx_uci.o \
    check_main.o \
    main.o

EXEC := oafd

LDLIBS += \
    -lubox \
    -lubus \
    -luci \
    -ljson-c \
    -lblobmsg_json \
    -lsqlite3 \
    -lcurl \
    -lpthread \
    -lm

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(EXEC) $(OBJS)
