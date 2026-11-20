PREFIX ?= /usr
DATADIR ?= $(PREFIX)/share/aerial-lock
BINDIR ?= $(PREFIX)/bin
LIBEXECDIR ?= $(PREFIX)/libexec/aerial-lock
AUTH_HELPER = $(LIBEXECDIR)/aerial-lock-auth
EMERGENCY_CLIENT = $(LIBEXECDIR)/aerial-lock-emergency
PLUGINROOT ?= $(PREFIX)/lib/aerial-lock/qt-plugins
PAMDIR ?= /etc/pam.d
CONFDIR ?= $(DATADIR)/Config
I18NDIR ?= $(CONFDIR)/i18n

PAM_MAX_RESP_SIZE ?=
DEBUG_DISMISS ?= 0
DEBUG_VERBOSE ?= 0

# Qt's protected data symbols require PIC, including LTO's link-time codegen.
# Keep every object and final link of the Qt consumers on the same model.
QT_PIC_FLAGS := -fPIC
NATIVE_DEPFLAGS := -MMD -MP

WAYLAND_PROTOCOLS ?= /usr/share/wayland-protocols
SESSION_LOCK_XML := $(WAYLAND_PROTOCOLS)/staging/ext-session-lock/ext-session-lock-v1.xml
IDLE_NOTIFY_XML := $(WAYLAND_PROTOCOLS)/staging/ext-idle-notify/ext-idle-notify-v1.xml

# Native objects stay inside this tree (gitignored, removed by `clean`), so
# concurrent builds of different checkouts never share or mix objects, and
# nothing is written to a global temporary directory. Every native rule
# honours the standard CPPFLAGS/CFLAGS/CXXFLAGS/LDFLAGS (e.g. makepkg's
# hardening flags).
OBJDIR ?= build
UNLOCK_OBJS := $(OBJDIR)/aerial-unlock-proto.o $(OBJDIR)/aerial-unlock-main.o
SUPERVISOR_OBJS := $(OBJDIR)/aerial-supervisor-proto.o $(OBJDIR)/aerial-supervisor-main.o $(OBJDIR)/config-document.o $(OBJDIR)/owned-directory.o $(OBJDIR)/content-snapshot.o
GUARD_OBJS := $(OBJDIR)/connection-guard.o
CONFIG_OBJS := $(OBJDIR)/config-document.o
CONTENT_OBJS := $(OBJDIR)/owned-directory.o $(OBJDIR)/content-snapshot.o $(OBJDIR)/source-resolver.o $(OBJDIR)/cache-store.o
PROBE_OBJS := $(OBJDIR)/media-probe.o
IDLE_OBJS := $(OBJDIR)/aerial-idle-main.o $(OBJDIR)/aerial-idle-proto.o $(CONFIG_OBJS)
COMPANION_CONTENT_OBJS := $(CONTENT_OBJS) $(OBJDIR)/http-operation.o $(OBJDIR)/manifest-store.o $(OBJDIR)/local-probe.o $(OBJDIR)/transfer.o $(OBJDIR)/planner-state.o $(OBJDIR)/acquisition-planner.o $(OBJDIR)/transfer-queue.o
COMPANION_OBJS := $(OBJDIR)/aerial-main.o $(OBJDIR)/config-repository.o $(OBJDIR)/aerial-service.o $(OBJDIR)/companion_adaptor.o $(OBJDIR)/job-object.o $(OBJDIR)/job_adaptor.o $(OBJDIR)/policy-monitor.o $(CONFIG_OBJS) $(COMPANION_CONTENT_OBJS)
CLIENT_BUILD := $(OBJDIR)/client
empty :=
space := $(empty) $(empty)
# Keep the actual compiler stable when a launcher is added to CXX. Changing
# CMAKE_CXX_COMPILER to the wrapper resets CMake's cache and drops its arguments.
# CXX is a program chain followed by compiler options. The final program is
# the compiler; preceding programs are launchers, not compiler arguments.
cxx_programs = $(if $(strip $(1)),$(if $(filter -%,$(firstword $(1))),,$(firstword $(1)) $(call cxx_programs,$(wordlist 2,$(words $(1)),$(1)))))
cxx_arguments = $(if $(strip $(1)),$(if $(filter -%,$(firstword $(1))),$(1),$(call cxx_arguments,$(wordlist 2,$(words $(1)),$(1)))))
CMAKE_CXX_PROGRAMS = $(strip $(call cxx_programs,$(CXX)))
CMAKE_CXX_COMPILER = $(lastword $(CMAKE_CXX_PROGRAMS))
CMAKE_CXX_LAUNCHER = $(subst $(space),;,$(filter-out $(CMAKE_CXX_COMPILER),$(CMAKE_CXX_PROGRAMS)))
CMAKE_CXX_ARGUMENTS = $(call cxx_arguments,$(CXX))
CLIENT_ARCHIVE := $(CLIENT_BUILD)/libaerial-client.a
SETTINGS_BUILD := $(OBJDIR)/settings
CLI_OBJS := $(OBJDIR)/aerialctl.o
AUTH_OBJS := $(OBJDIR)/aerial-lock-auth.o
EMERGENCY_OBJS := $(OBJDIR)/emergency-main.o $(OBJDIR)/emergency-wayland.o $(OBJDIR)/emergency-render.o $(OBJDIR)/emergency-input.o $(OBJDIR)/emergency-proto.o $(OBJDIR)/auth-client.o
INPUT_OBJS := $(OBJDIR)/surface-input.o $(OBJDIR)/auth-context.o $(OBJDIR)/auth-client.o $(OBJDIR)/power-actions.o $(OBJDIR)/local-config.o $(OBJDIR)/local-content.o $(OBJDIR)/companion-hint.o $(OBJDIR)/owned-directory.o $(OBJDIR)/content-snapshot.o $(CONFIG_OBJS)
GUARD_PLUGIN := native/qt-plugins/generic/libAerialLockConnectionGuard.so
INPUT_PLUGIN := native/qt-plugins/generic/libAerialLockSurfaceInput.so

.PHONY: all install uninstall dev clean pam-limits build-flags force-native-config

all: pam-limits build-flags recovery/aerial-unlock supervisor/aerial-lock-supervisor idle/aerial-idle native/aerial-lock-auth emergency/aerial-lock-emergency $(GUARD_PLUGIN) $(INPUT_PLUGIN) companion/aerial companion/aerial-media-probe cli/aerialctl settings/aerial-settings

# Let the compiler own the complete transitive header graph. Private flags do
# not leak into shared code-generation/config prerequisites or link commands.
NATIVE_OBJS := $(sort $(UNLOCK_OBJS) $(SUPERVISOR_OBJS) $(GUARD_OBJS) $(IDLE_OBJS) $(AUTH_OBJS) $(EMERGENCY_OBJS) $(COMPANION_OBJS) $(PROBE_OBJS) $(CLI_OBJS) $(INPUT_OBJS))
$(NATIVE_OBJS): private override CPPFLAGS += $(NATIVE_DEPFLAGS)
-include $(wildcard $(OBJDIR)/*.d)

# Make cannot see variable changes in a command line. One content-stable
# prerequisite records the effective native configuration, so an incremental
# install cannot reuse objects compiled for a different prefix or flag set.
define NATIVE_CONFIG
CC=$(CC)
CXX=$(CXX)
CPPFLAGS=$(CPPFLAGS)
CFLAGS=$(CFLAGS)
CXXFLAGS=$(CXXFLAGS)
LDFLAGS=$(LDFLAGS)
QT_PIC_FLAGS=$(QT_PIC_FLAGS)
NATIVE_DEPFLAGS=$(NATIVE_DEPFLAGS)
DATADIR=$(DATADIR)
BINDIR=$(BINDIR)
LIBEXECDIR=$(LIBEXECDIR)
AUTH_HELPER=$(AUTH_HELPER)
EMERGENCY_CLIENT=$(EMERGENCY_CLIENT)
PLUGINROOT=$(PLUGINROOT)
endef

$(OBJDIR):
	@mkdir -p $@

$(OBJDIR)/native-config: force-native-config | $(OBJDIR)
	$(file >$@.tmp,$(NATIVE_CONFIG))
	@cmp -s $@.tmp $@ && rm -f $@.tmp || mv -f $@.tmp $@

$(UNLOCK_OBJS) $(SUPERVISOR_OBJS) $(GUARD_OBJS) $(IDLE_OBJS) $(AUTH_OBJS) $(EMERGENCY_OBJS) $(COMPANION_OBJS) $(CONTENT_OBJS) $(PROBE_OBJS) $(CLI_OBJS): $(OBJDIR)/native-config

# Phony on purpose: the value can come from the environment, which Make
# cannot express as a file prerequisite. The generator is sub-second, so
# unconditional regeneration removes the stale-artifact bug class for free.
pam-limits:
ifdef PAM_MAX_RESP_SIZE
	@printf '{"maxResponseSize": %d, "source": "build-override"}\n' \
		$(PAM_MAX_RESP_SIZE) > Config/pam-limits.json
else
	@$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o gen-pam-limits.tmp gen-pam-limits.c
	@./gen-pam-limits.tmp > Config/pam-limits.json
	@rm -f gen-pam-limits.tmp
endif
	@cat Config/pam-limits.json

# Phony on purpose, same rationale as pam-limits: the values come from the
# environment, which Make cannot express as a file prerequisite. The two
# flags are independent — DEBUG_VERBOSE=1 alone gives a chatty build with no
# authentication bypass.
build-flags:
	@printf '{ "debugAllowDismiss": %s, "verbose": %s }\n' \
		$(if $(filter 1,$(DEBUG_DISMISS)),true,false) \
		$(if $(filter 1,$(DEBUG_VERBOSE)),true,false) > Config/build-flags.json
	@cat Config/build-flags.json

recovery/ext-session-lock-v1-client.h: $(SESSION_LOCK_XML)
	wayland-scanner client-header $< $@

recovery/ext-session-lock-v1-protocol.c: $(SESSION_LOCK_XML)
	wayland-scanner private-code $< $@

$(OBJDIR)/aerial-unlock-proto.o: recovery/ext-session-lock-v1-protocol.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< \
		$$(pkg-config --cflags wayland-client) -o $@

$(OBJDIR)/aerial-unlock-main.o: recovery/aerial-unlock.cpp recovery/ext-session-lock-v1-client.h
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -Irecovery \
		$$(pkg-config --cflags wayland-client) -o $@

recovery/aerial-unlock: $(UNLOCK_OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(OBJDIR)/aerial-unlock-main.o $(OBJDIR)/aerial-unlock-proto.o \
		$$(pkg-config --libs wayland-client)

supervisor/ext-session-lock-v1-client.h: $(SESSION_LOCK_XML)
	wayland-scanner client-header $< $@

supervisor/ext-session-lock-v1-protocol.c: $(SESSION_LOCK_XML)
	wayland-scanner private-code $< $@

supervisor/aerial-lock-supervisor.moc: supervisor/aerial-lock-supervisor.cpp
	/usr/lib/qt6/moc $< -o $@

$(OBJDIR)/aerial-supervisor-proto.o: supervisor/ext-session-lock-v1-protocol.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(QT_PIC_FLAGS) -c $< \
		$$(pkg-config --cflags wayland-client) -o $@

$(OBJDIR)/aerial-supervisor-main.o: supervisor/aerial-lock-supervisor.cpp supervisor/emergency-settings.h supervisor/aerial-lock-supervisor.moc supervisor/ext-session-lock-v1-client.h native/shell-dir.h common/config-document.h common/content-snapshot.h
	@mkdir -p $(@D)
	$(CXX) -std=c++17 $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS) -c $< -Isupervisor \
		-DAERIAL_LOCK_DEFAULT_SHELL='"$(DATADIR)"' \
		-DAERIAL_LOCK_DEFAULT_RUNNER='"$(LIBEXECDIR)/aerial-lock-run"' \
		-DAERIAL_LOCK_DEFAULT_PLUGIN_ROOT='"$(PLUGINROOT)"' \
		-DAERIAL_LOCK_DEFAULT_EMERGENCY='"$(EMERGENCY_CLIENT)"' \
		$$(pkg-config --cflags Qt6Core Qt6DBus wayland-client) -o $@

native/connection-guard.moc: native/connection-guard.cpp native/connection-guard.json
	/usr/lib/qt6/moc $$(pkg-config --cflags Qt6Core Qt6Gui wayland-client) $< -o $@

$(OBJDIR)/connection-guard.o: native/connection-guard.cpp native/connection-guard.moc native/connection-guard.json
	@mkdir -p $(@D)
	$(CXX) -std=c++17 $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS) -pthread -c $< \
		$$(pkg-config --cflags Qt6Core Qt6Gui wayland-client) -o $@

$(GUARD_PLUGIN): $(GUARD_OBJS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(QT_PIC_FLAGS) -shared -pthread -o $@ $(GUARD_OBJS) \
		$$(pkg-config --libs Qt6Core Qt6Gui wayland-client) -ldl

supervisor/aerial-lock-supervisor: $(SUPERVISOR_OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(QT_PIC_FLAGS) -o $@ $(SUPERVISOR_OBJS) \
		$$(pkg-config --libs Qt6Core Qt6DBus wayland-client)

native/surface-input.moc: native/surface-input.cpp native/surface-input.h native/surface-input.json native/auth-context.h native/power-actions.h native/local-config.h native/local-content.h native/companion-client.h
	/usr/lib/qt6/moc $$(pkg-config --cflags Qt6Quick) $< -o $@

$(OBJDIR)/surface-input.o: native/surface-input.cpp native/surface-input.h native/surface-input.moc native/power-actions.h native/local-config.h native/local-content.h native/companion-client.h $(OBJDIR)/native-config
	$(CXX) -std=c++17 $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS) -c $< $$(pkg-config --cflags Qt6Quick Qt6DBus) -o $@

native/auth-context.moc: native/auth-context.h
	/usr/lib/qt6/moc $$(pkg-config --cflags Qt6Core) $< -o $@

$(OBJDIR)/auth-context.o: native/auth-context.cpp native/auth-context.h native/auth-context.moc native/auth-client.h native/auth-protocol.h $(OBJDIR)/native-config
	$(CXX) -std=c++17 $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS) -c $< \
		-DAERIAL_LOCK_AUTH_HELPER='"$(AUTH_HELPER)"' $$(pkg-config --cflags Qt6Core) -o $@

$(OBJDIR)/auth-client.o: native/auth-client.c native/auth-client.h native/auth-protocol.h $(OBJDIR)/native-config
	$(CC) -std=c11 $(CPPFLAGS) $(CFLAGS) $(QT_PIC_FLAGS) -c $< -o $@

$(OBJDIR)/aerial-lock-auth.o: native/aerial-lock-auth.c native/auth-protocol.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

native/power-actions.moc: native/power-actions.h
	/usr/lib/qt6/moc $$(pkg-config --cflags Qt6Core Qt6DBus) $< -o $@

$(OBJDIR)/power-actions.o: native/power-actions.cpp native/power-actions.h native/power-actions.moc $(OBJDIR)/native-config
	$(CXX) -std=c++17 $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS) -c $< $$(pkg-config --cflags Qt6Core Qt6DBus) -o $@

native/aerial-lock-auth: $(AUTH_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -lpam

emergency/ext-session-lock-v1-client.h: $(SESSION_LOCK_XML)
	wayland-scanner client-header $< $@

emergency/ext-session-lock-v1-protocol.c: $(SESSION_LOCK_XML)
	wayland-scanner private-code $< $@

$(OBJDIR)/emergency-proto.o: emergency/ext-session-lock-v1-protocol.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< $$(pkg-config --cflags wayland-client) -o $@

$(OBJDIR)/emergency-main.o: emergency/aerial-lock-emergency.c emergency/locker.h emergency/ext-session-lock-v1-client.h native/auth-client.h native/auth-protocol.h
	$(CC) -std=c11 $(CPPFLAGS) $(CFLAGS) -c $< -Iemergency -DAERIAL_LOCK_AUTH_HELPER='"$(AUTH_HELPER)"' $$(pkg-config --cflags wayland-client xkbcommon) -o $@

$(OBJDIR)/emergency-%.o: emergency/%.c emergency/locker.h emergency/ext-session-lock-v1-client.h native/auth-client.h native/auth-protocol.h
	$(CC) -std=c11 $(CPPFLAGS) $(CFLAGS) -c $< -Iemergency $$(pkg-config --cflags wayland-client xkbcommon) -o $@

emergency/aerial-lock-emergency: $(EMERGENCY_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(EMERGENCY_OBJS) -Wl,--as-needed $$(pkg-config --libs wayland-client xkbcommon)

$(INPUT_PLUGIN): $(INPUT_OBJS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(QT_PIC_FLAGS) -shared -o $@ $(INPUT_OBJS) $$(pkg-config --libs Qt6Quick Qt6DBus) -ldl

idle/ext-idle-notify-v1-client.h: $(IDLE_NOTIFY_XML)
	wayland-scanner client-header $< $@

idle/ext-idle-notify-v1-protocol.c: $(IDLE_NOTIFY_XML)
	wayland-scanner private-code $< $@

idle/aerial-idle.moc: idle/aerial-idle.cpp
	/usr/lib/qt6/moc $< -o $@

$(OBJDIR)/aerial-idle-proto.o: idle/ext-idle-notify-v1-protocol.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(QT_PIC_FLAGS) -c $< $$(pkg-config --cflags wayland-client) -o $@

$(OBJDIR)/aerial-idle-main.o: idle/aerial-idle.cpp idle/aerial-idle.moc idle/ext-idle-notify-v1-client.h native/session-identity.h native/shell-dir.h common/config-document.h
	$(CXX) -std=c++17 $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS) -c $< -Iidle \
		-DAERIAL_IDLE_DATADIR='"$(DATADIR)"' -DAERIAL_IDLE_LOCK_COMMAND='"$(BINDIR)/aerial-lock"' \
		$$(pkg-config --cflags Qt6Core Qt6DBus wayland-client) -o $@

idle/aerial-idle: $(IDLE_OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(QT_PIC_FLAGS) -o $@ $(IDLE_OBJS) $$(pkg-config --libs Qt6Core Qt6DBus wayland-client)

$(OBJDIR)/config-document.o: common/config-document.cpp common/config-document.h $(OBJDIR)/native-config
	$(CXX) -std=c++17 $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS) -c $< $$(pkg-config --cflags Qt6Core) -o $@

$(OBJDIR)/local-config.moc: native/local-config.h | $(OBJDIR)
	/usr/lib/qt6/moc $< -o $@

$(OBJDIR)/local-config.o: native/local-config.cpp native/local-config.h common/config-document.h $(OBJDIR)/local-config.moc $(OBJDIR)/native-config
	$(CXX) -std=c++17 $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS) -I$(OBJDIR) -c $< $$(pkg-config --cflags Qt6Core) -o $@

$(OBJDIR)/local-content.moc: native/local-content.h | $(OBJDIR)
	/usr/lib/qt6/moc $< -o $@

$(OBJDIR)/local-content.o: native/local-content.cpp native/local-content.h common/content-snapshot.h $(OBJDIR)/local-content.moc $(OBJDIR)/native-config
	$(CXX) -std=c++17 $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS) -I$(OBJDIR) -c $< $$(pkg-config --cflags Qt6Core) -o $@

$(OBJDIR)/companion-client.moc: native/companion-client.h | $(OBJDIR)
	/usr/lib/qt6/moc $< -o $@

$(OBJDIR)/companion-hint.o: native/companion-client.cpp native/companion-client.h common/config-document.h $(OBJDIR)/companion-client.moc $(OBJDIR)/native-config
	$(CXX) -std=c++17 $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS) -I$(OBJDIR) -DAERIAL_COMPANION_DATADIR='"$(DATADIR)"' -c $< $$(pkg-config --cflags Qt6Core Qt6DBus) -o $@

$(OBJDIR)/config-repository.moc: companion/config-repository.h | $(OBJDIR)
	/usr/lib/qt6/moc $< -o $@

$(OBJDIR)/service.moc: companion/service.h | $(OBJDIR)
	/usr/lib/qt6/moc $< -o $@

$(OBJDIR)/companion_adaptor.cpp $(OBJDIR)/companion_adaptor.h &: dbus/org.aeriallock.Aerial1.xml | $(OBJDIR)
	/usr/lib/qt6/bin/qdbusxml2cpp -m -a $(OBJDIR)/companion_adaptor -l Aerial::CompanionService -i companion/service.h $<

$(OBJDIR)/config-repository.o: companion/config-repository.cpp companion/config-repository.h common/config-document.h $(OBJDIR)/config-repository.moc
	$(CXX) -std=c++17 $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS) -I$(OBJDIR) -c $< $$(pkg-config --cflags Qt6Core) -o $@

$(OBJDIR)/aerial-service.o: companion/service.cpp companion/service.h companion/config-repository.h companion/job-object.h companion/cache-store.h companion/transfer-queue.h companion/planner-state.h companion/policy-monitor.h common/dbus-types.h $(OBJDIR)/companion_adaptor.h $(OBJDIR)/service.moc
	$(CXX) -std=c++17 $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS) -I. -I$(OBJDIR) -c $< $$(pkg-config --cflags Qt6Core Qt6DBus Qt6Network) -o $@

$(OBJDIR)/job_adaptor.cpp $(OBJDIR)/job_adaptor.h &: dbus/org.aeriallock.Aerial1.Job1.xml | $(OBJDIR)
	/usr/lib/qt6/bin/qdbusxml2cpp -m -a $(OBJDIR)/job_adaptor -l Aerial::JobObject -i companion/job-object.h $<

$(OBJDIR)/moc_job_adaptor.cpp: $(OBJDIR)/job_adaptor.h
	/usr/lib/qt6/moc $< -o $@

$(OBJDIR)/job_adaptor.o: $(OBJDIR)/job_adaptor.cpp $(OBJDIR)/job_adaptor.h $(OBJDIR)/moc_job_adaptor.cpp companion/job-object.h
	$(CXX) -std=c++17 $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS) -I. -I$(OBJDIR) -c $< $$(pkg-config --cflags Qt6Core Qt6DBus) -o $@

$(OBJDIR)/job-object.o: companion/job-object.cpp companion/job-object.h $(OBJDIR)/job_adaptor.h $(OBJDIR)/job-object.moc
	$(CXX) -std=c++17 $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS) -I. -I$(OBJDIR) -c companion/job-object.cpp $$(pkg-config --cflags Qt6Core Qt6DBus) -o $@

$(CLIENT_ARCHIVE): common/client/CMakeLists.txt $(wildcard common/client/*.cpp common/client/*.h) common/config-document.cpp common/config-document.h common/dbus-types.h dbus/org.aeriallock.Aerial1.xml dbus/org.aeriallock.Aerial1.Job1.xml $(OBJDIR)/native-config
	cmake -S common/client -B "$(CLIENT_BUILD)" -G Ninja -DCMAKE_CXX_COMPILER="$(CMAKE_CXX_COMPILER)" -DCMAKE_CXX_COMPILER_LAUNCHER="$(CMAKE_CXX_LAUNCHER)" -DCMAKE_CXX_FLAGS="$(CMAKE_CXX_ARGUMENTS) $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS)"
	cmake --build "$(CLIENT_BUILD)" -j2

$(OBJDIR)/aerialctl.o: cli/aerialctl.cpp $(CLIENT_ARCHIVE) common/config-document.h common/dbus-types.h native/shell-dir.h
	$(CXX) -std=c++17 $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS) -I. -I"$(CLIENT_BUILD)" -DAERIAL_COMPANION_DATADIR='"$(DATADIR)"' -c cli/aerialctl.cpp $$(pkg-config --cflags Qt6Core Qt6DBus) -o $@

cli/aerialctl: $(CLI_OBJS) $(CLIENT_ARCHIVE)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(QT_PIC_FLAGS) -o $@ $(CLI_OBJS) $(CLIENT_ARCHIVE) $$(pkg-config --libs Qt6Core Qt6DBus)

settings/aerial-settings: native/shell-dir.h $(CLIENT_ARCHIVE) $(shell find settings -type f ! -name aerial-settings) $(OBJDIR)/native-config
	cmake -S settings -B "$(SETTINGS_BUILD)" -G Ninja -DCMAKE_CXX_COMPILER="$(CMAKE_CXX_COMPILER)" -DCMAKE_CXX_COMPILER_LAUNCHER="$(CMAKE_CXX_LAUNCHER)" -DCMAKE_CXX_FLAGS="$(CMAKE_CXX_ARGUMENTS) $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS)" -DCMAKE_EXE_LINKER_FLAGS="$(LDFLAGS)" -DAERIAL_COMPANION_DATADIR="$(DATADIR)" -DAERIAL_CLIENT_ARCHIVE="$(abspath $(CLIENT_ARCHIVE))"
	cmake --build "$(SETTINGS_BUILD)" -j2
	cp "$(SETTINGS_BUILD)/aerial-settings" $@

$(OBJDIR)/moc_companion_adaptor.cpp: $(OBJDIR)/companion_adaptor.h
	/usr/lib/qt6/moc $< -o $@

$(OBJDIR)/companion_adaptor.o: $(OBJDIR)/companion_adaptor.cpp $(OBJDIR)/companion_adaptor.h $(OBJDIR)/moc_companion_adaptor.cpp companion/service.h
	$(CXX) -std=c++17 $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS) -I. -I$(OBJDIR) -c $< $$(pkg-config --cflags Qt6Core Qt6DBus) -o $@

$(OBJDIR)/policy-monitor.o: companion/policy-monitor.cpp companion/policy-monitor.h $(OBJDIR)/policy-monitor.moc
	$(CXX) -std=c++17 $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS) -I$(OBJDIR) -c companion/policy-monitor.cpp $$(pkg-config --cflags Qt6Core Qt6DBus) -o $@

$(OBJDIR)/aerial-main.o: companion/main.cpp companion/config-repository.h companion/service.h companion/transfer-queue.h companion/policy-monitor.h common/config-document.h native/shell-dir.h
	$(CXX) -std=c++17 $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS) -DAERIAL_COMPANION_DATADIR='"$(DATADIR)"' -DAERIAL_MEDIA_PROBE_PATH='"$(LIBEXECDIR)/aerial-media-probe"' -c $< $$(pkg-config --cflags Qt6Core Qt6DBus Qt6Network) -o $@

companion/aerial: $(COMPANION_OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(QT_PIC_FLAGS) -o $@ $(COMPANION_OBJS) $$(pkg-config --libs Qt6Core Qt6DBus Qt6Network libarchive)

$(OBJDIR)/%.moc: companion/%.h | $(OBJDIR)
	/usr/lib/qt6/moc $< -o $@

$(OBJDIR)/http-operation.o: companion/http-operation.cpp companion/http-operation.h companion/source-resolver.h $(OBJDIR)/http-operation.moc
$(OBJDIR)/manifest-store.o: companion/manifest-store.cpp companion/manifest-store.h companion/cache-store.h $(OBJDIR)/manifest-store.moc
$(OBJDIR)/local-probe.o: companion/local-probe.cpp companion/local-probe.h $(OBJDIR)/local-probe.moc
$(OBJDIR)/transfer.o: companion/transfer.cpp companion/transfer.h companion/cache-store.h companion/http-operation.h $(OBJDIR)/transfer.moc
$(OBJDIR)/planner-state.o: companion/planner-state.cpp companion/planner-state.h common/owned-directory.h
$(OBJDIR)/acquisition-planner.o: companion/acquisition-planner.cpp companion/acquisition-planner.h companion/planner-state.h companion/source-resolver.h
$(OBJDIR)/transfer-queue.o: companion/transfer-queue.cpp companion/transfer-queue.h companion/config-repository.h companion/acquisition-planner.h companion/manifest-store.h companion/transfer.h $(OBJDIR)/transfer-queue.moc

$(filter-out $(CONTENT_OBJS),$(COMPANION_CONTENT_OBJS)):
	$(CXX) -std=c++17 $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS) -I$(OBJDIR) -c companion/$(notdir $(@:.o=.cpp)) $$(pkg-config --cflags Qt6Core Qt6Network) -o $@

$(OBJDIR)/owned-directory.o: common/owned-directory.cpp common/owned-directory.h
	$(CXX) -std=c++17 $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS) -c $< $$(pkg-config --cflags Qt6Core) -o $@

$(OBJDIR)/content-snapshot.o: common/content-snapshot.cpp common/content-snapshot.h common/owned-directory.h
	$(CXX) -std=c++17 $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS) -c $< $$(pkg-config --cflags Qt6Core) -o $@

$(OBJDIR)/source-resolver.o: companion/source-resolver.cpp companion/source-resolver.h common/content-snapshot.h
	$(CXX) -std=c++17 $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS) -c $< $$(pkg-config --cflags Qt6Core libarchive) -o $@

$(OBJDIR)/cache-store.o: companion/cache-store.cpp companion/cache-store.h companion/source-resolver.h common/owned-directory.h common/content-snapshot.h
	$(CXX) -std=c++17 $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS) -c $< $$(pkg-config --cflags Qt6Core) -o $@

$(OBJDIR)/media-probe.o: companion/media-probe.cpp
	$(CXX) -std=c++17 $(CPPFLAGS) $(CXXFLAGS) $(QT_PIC_FLAGS) -c $< $$(pkg-config --cflags Qt6Core libavformat libavcodec libavutil) -o $@

companion/aerial-media-probe: $(PROBE_OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(QT_PIC_FLAGS) -o $@ $(PROBE_OBJS) $$(pkg-config --libs Qt6Core libavformat libavcodec libavutil)

install: all
	install -d $(DESTDIR)$(DATADIR)
	install -d $(DESTDIR)$(DATADIR)/Modules/Lock
	install -d $(DESTDIR)$(DATADIR)/Services
	install -d $(DESTDIR)$(CONFDIR)
	install -d $(DESTDIR)$(I18NDIR)
	install -d $(DESTDIR)$(BINDIR)
	install -m 644 shell.qml $(DESTDIR)$(DATADIR)/shell.qml
	install -m 644 Modules/Lock/qmldir $(DESTDIR)$(DATADIR)/Modules/Lock/
	install -m 644 Modules/Lock/Lock.qml $(DESTDIR)$(DATADIR)/Modules/Lock/
	install -m 644 Modules/Lock/LockContent.qml $(DESTDIR)$(DATADIR)/Modules/Lock/
	install -m 644 Modules/Lock/LockBackground.qml $(DESTDIR)$(DATADIR)/Modules/Lock/
	install -m 644 Modules/Lock/AccentSampler.qml $(DESTDIR)$(DATADIR)/Modules/Lock/
	install -m 644 Modules/Lock/AccentTone.js $(DESTDIR)$(DATADIR)/Modules/Lock/
	install -m 644 Modules/Lock/ThemeHost.qml $(DESTDIR)$(DATADIR)/Modules/Lock/
	install -m 644 Modules/Lock/AuthField.qml $(DESTDIR)$(DATADIR)/Modules/Lock/
	install -m 644 Modules/Lock/AuthEditor.qml Modules/Lock/AuthInputSink.qml $(DESTDIR)$(DATADIR)/Modules/Lock/
	install -m 644 Modules/Lock/Avatar.qml $(DESTDIR)$(DATADIR)/Modules/Lock/
	install -m 644 Modules/Lock/FrostedText.qml $(DESTDIR)$(DATADIR)/Modules/Lock/
	install -m 644 Modules/Lock/PowerActionControls.qml Modules/Lock/PowerActionButton.qml $(DESTDIR)$(DATADIR)/Modules/Lock/
	install -m 644 Modules/Lock/PowerActionMaterial.qml $(DESTDIR)$(DATADIR)/Modules/Lock/
	install -d $(DESTDIR)$(DATADIR)/Themes
	install -m 644 Themes/Macintosh.qml $(DESTDIR)$(DATADIR)/Themes/
	install -m 644 Themes/Windows7.qml $(DESTDIR)$(DATADIR)/Themes/
	install -m 644 Themes/MaterialYou.qml $(DESTDIR)$(DATADIR)/Themes/
	install -m 644 Themes/Enfield.qml $(DESTDIR)$(DATADIR)/Themes/
	install -m 644 Themes/Clockwork.qml $(DESTDIR)$(DATADIR)/Themes/
	# d44 font bundles + attribution/notices (OFL 1.1; see Themes/ATTRIBUTION.md)
	install -m 644 Themes/ATTRIBUTION.md $(DESTDIR)$(DATADIR)/Themes/
	install -d $(DESTDIR)$(DATADIR)/Themes/fonts/google-sans
	install -m 644 "Themes/fonts/google-sans/GoogleSans-VariableFont_GRAD,opsz,wght.ttf" $(DESTDIR)$(DATADIR)/Themes/fonts/google-sans/
	install -m 644 Themes/fonts/google-sans/OFL.txt $(DESTDIR)$(DATADIR)/Themes/fonts/google-sans/
	install -m 644 Themes/fonts/google-sans/NOTICE $(DESTDIR)$(DATADIR)/Themes/fonts/google-sans/
	install -d $(DESTDIR)$(DATADIR)/Themes/fonts/orbitron
	install -m 644 Themes/fonts/orbitron/Orbitron-VariableFont_wght.ttf $(DESTDIR)$(DATADIR)/Themes/fonts/orbitron/
	install -m 644 Themes/fonts/orbitron/OFL.txt $(DESTDIR)$(DATADIR)/Themes/fonts/orbitron/
	install -m 644 Themes/fonts/orbitron/NOTICE $(DESTDIR)$(DATADIR)/Themes/fonts/orbitron/
	install -d $(DESTDIR)$(DATADIR)/Themes/fonts/outfit
	install -m 644 Themes/fonts/outfit/Outfit-Black.ttf $(DESTDIR)$(DATADIR)/Themes/fonts/outfit/
	install -m 644 Themes/fonts/outfit/OFL.txt $(DESTDIR)$(DATADIR)/Themes/fonts/outfit/
	install -m 644 Themes/fonts/outfit/NOTICE $(DESTDIR)$(DATADIR)/Themes/fonts/outfit/
	install -m 644 Services/qmldir $(DESTDIR)$(DATADIR)/Services/
	install -m 644 Services/Logger.qml $(DESTDIR)$(DATADIR)/Services/
	install -m 644 Services/ConfigStore.qml $(DESTDIR)$(DATADIR)/Services/
	install -m 644 Services/PamLimits.qml $(DESTDIR)$(DATADIR)/Services/
	install -m 644 Services/PamProbe.qml $(DESTDIR)$(DATADIR)/Services/
	install -m 644 Services/BuildFlags.qml $(DESTDIR)$(DATADIR)/Services/
	install -m 644 Services/Compositor.qml $(DESTDIR)$(DATADIR)/Services/
	install -m 644 Services/SourceCache.qml $(DESTDIR)$(DATADIR)/Services/
	install -m 644 Services/ThemeSelection.js $(DESTDIR)$(DATADIR)/Services/
	install -m 644 Services/LockContext.qml $(DESTDIR)$(DATADIR)/Services/
	install -m 644 Services/AuthInputState.qml $(DESTDIR)$(DATADIR)/Services/
	install -m 644 Services/EngagementReport.qml $(DESTDIR)$(DATADIR)/Services/
	install -d $(DESTDIR)$(DATADIR)/Services/Compositor
	install -m 644 Services/Compositor/HyprlandBackend.qml $(DESTDIR)$(DATADIR)/Services/Compositor/
	install -m 644 Config/defaults.json $(DESTDIR)$(CONFDIR)/
	install -m 644 Config/settings-schema.json $(DESTDIR)$(CONFDIR)/
	install -m 644 Config/apple-root.pem $(DESTDIR)$(CONFDIR)/
	install -m 644 Config/pam-limits.json $(DESTDIR)$(CONFDIR)/
	install -m 644 Config/build-flags.json $(DESTDIR)$(CONFDIR)/
	install -m 644 Config/i18n/en.json $(DESTDIR)$(I18NDIR)/
	install -m 755 aerial-lock $(DESTDIR)$(BINDIR)/aerial-lock
	install -m 755 cli/aerialctl $(DESTDIR)$(BINDIR)/aerialctl
	install -m 755 Scripts/aerial-lock-fetch $(DESTDIR)$(BINDIR)/aerial-lock-fetch
	install -m 755 recovery/aerial-unlock $(DESTDIR)$(BINDIR)/aerial-unlock
	install -m 755 supervisor/aerial-lock-supervisor $(DESTDIR)$(BINDIR)/aerial-lock-supervisor
	install -m 755 idle/aerial-idle $(DESTDIR)$(BINDIR)/aerial-idle
	install -d $(DESTDIR)$(PREFIX)/lib/systemd/user $(DESTDIR)$(DATADIR)/examples
	sed 's|@BINDIR@|$(BINDIR)|g' systemd/aerial-idle.service.in > $(OBJDIR)/aerial-idle.service
	install -m 644 $(OBJDIR)/aerial-idle.service $(DESTDIR)$(PREFIX)/lib/systemd/user/
	sed 's|@BINDIR@|$(BINDIR)|g' systemd/aerial.service.in > $(OBJDIR)/aerial.service
	install -m 644 $(OBJDIR)/aerial.service $(DESTDIR)$(PREFIX)/lib/systemd/user/
	install -d $(DESTDIR)$(PREFIX)/lib/systemd/user/default.target.wants
	ln -sfn ../aerial.service $(DESTDIR)$(PREFIX)/lib/systemd/user/default.target.wants/aerial.service
	install -m 644 examples/idle.json $(DESTDIR)$(DATADIR)/examples/
	install -d $(DESTDIR)$(LIBEXECDIR) $(DESTDIR)$(PLUGINROOT)/generic
	install -m 755 Scripts/aerial-lock-run $(DESTDIR)$(LIBEXECDIR)/aerial-lock-run
	install -m 755 native/aerial-lock-auth $(DESTDIR)$(LIBEXECDIR)/aerial-lock-auth
	install -m 755 companion/aerial $(DESTDIR)$(BINDIR)/aerial
	install -m 755 companion/aerial-media-probe $(DESTDIR)$(LIBEXECDIR)/aerial-media-probe
	install -d $(DESTDIR)$(PREFIX)/share/dbus-1/interfaces
	install -d $(DESTDIR)$(PREFIX)/share/dbus-1/services
	sed 's|@BINDIR@|$(BINDIR)|g' dbus/org.aeriallock.Aerial1.service.in > $(OBJDIR)/org.aeriallock.Aerial1.service
	install -m 644 $(OBJDIR)/org.aeriallock.Aerial1.service $(DESTDIR)$(PREFIX)/share/dbus-1/services/
	install -m 644 dbus/org.aeriallock.Aerial1.xml $(DESTDIR)$(PREFIX)/share/dbus-1/interfaces/
	install -m 644 dbus/org.aeriallock.Aerial1.Job1.xml $(DESTDIR)$(PREFIX)/share/dbus-1/interfaces/
	install -m 755 emergency/aerial-lock-emergency $(DESTDIR)$(LIBEXECDIR)/aerial-lock-emergency
	install -d $(DESTDIR)$(DATADIR)/AerialLock/Native
	install -m 644 AerialLock/Native/qmldir AerialLock/Native/native.qmltypes $(DESTDIR)$(DATADIR)/AerialLock/Native/
	install -m 755 $(GUARD_PLUGIN) $(DESTDIR)$(PLUGINROOT)/generic/
	install -m 755 $(INPUT_PLUGIN) $(DESTDIR)$(PLUGINROOT)/generic/
	install -d $(DESTDIR)$(PAMDIR)
	install -m 644 pam.d/aerial-lock $(DESTDIR)$(PAMDIR)/aerial-lock
	install -d $(DESTDIR)$(PREFIX)/share/licenses/aerial-lock
	install -m 644 LICENSE docs/third-party-notices.md $(DESTDIR)$(PREFIX)/share/licenses/aerial-lock/

uninstall:
	rm -rf $(DESTDIR)$(DATADIR)
	rm -f $(DESTDIR)$(BINDIR)/aerial-lock
	rm -f $(DESTDIR)$(BINDIR)/aerialctl
	rm -f $(DESTDIR)$(BINDIR)/aerial-lock-fetch
	rm -f $(DESTDIR)$(BINDIR)/aerial-unlock
	rm -f $(DESTDIR)$(BINDIR)/aerial-lock-supervisor
	rm -f $(DESTDIR)$(BINDIR)/aerial-idle $(DESTDIR)$(PREFIX)/lib/systemd/user/aerial-idle.service
	rm -f $(DESTDIR)$(PREFIX)/lib/systemd/user/default.target.wants/aerial.service $(DESTDIR)$(PREFIX)/lib/systemd/user/aerial.service
	rm -f $(DESTDIR)$(PREFIX)/share/dbus-1/services/org.aeriallock.Aerial1.service
	rm -f $(DESTDIR)$(LIBEXECDIR)/aerial-lock-run $(DESTDIR)$(PLUGINROOT)/generic/libAerialLockConnectionGuard.so
	rm -f $(DESTDIR)$(PLUGINROOT)/generic/libAerialLockSurfaceInput.so
	rm -f $(DESTDIR)$(LIBEXECDIR)/aerial-lock-auth
	rm -f $(DESTDIR)$(BINDIR)/aerial $(DESTDIR)$(PREFIX)/share/dbus-1/interfaces/org.aeriallock.Aerial1.xml
	rm -f $(DESTDIR)$(LIBEXECDIR)/aerial-media-probe
	rm -f $(DESTDIR)$(PREFIX)/share/dbus-1/interfaces/org.aeriallock.Aerial1.Job1.xml
	rm -f $(DESTDIR)$(LIBEXECDIR)/aerial-lock-emergency
	rm -f $(DESTDIR)$(PAMDIR)/aerial-lock
	rm -f $(DESTDIR)$(PREFIX)/share/licenses/aerial-lock/LICENSE $(DESTDIR)$(PREFIX)/share/licenses/aerial-lock/third-party-notices.md

clean:
	$(if $(wildcard $(SETTINGS_BUILD)/build.ninja),cmake --build "$(SETTINGS_BUILD)" --target clean, :)
	rm -f settings/aerial-settings
	$(if $(wildcard $(CLIENT_BUILD)/build.ninja),cmake --build "$(CLIENT_BUILD)" --target clean, :)
	rm -f gen-pam-limits.tmp Config/pam-limits.json Config/build-flags.json
	rm -f recovery/aerial-unlock recovery/ext-session-lock-v1-client.h recovery/ext-session-lock-v1-protocol.c
	rm -f supervisor/aerial-lock-supervisor supervisor/aerial-lock-supervisor.moc supervisor/ext-session-lock-v1-client.h supervisor/ext-session-lock-v1-protocol.c
	rm -f native/connection-guard.moc $(GUARD_PLUGIN)
	rm -f native/surface-input.moc $(INPUT_PLUGIN) $(OBJDIR)/surface-input.o
	rm -f native/auth-context.moc native/aerial-lock-auth $(INPUT_OBJS) $(AUTH_OBJS)
	rm -f native/power-actions.moc
	rm -f idle/aerial-idle idle/aerial-idle.moc idle/ext-idle-notify-v1-client.h idle/ext-idle-notify-v1-protocol.c
	rm -f $(UNLOCK_OBJS) $(SUPERVISOR_OBJS) $(GUARD_OBJS) $(IDLE_OBJS)
	rm -f emergency/aerial-lock-emergency emergency/ext-session-lock-v1-client.h emergency/ext-session-lock-v1-protocol.c $(EMERGENCY_OBJS)
	rm -f $(OBJDIR)/native-config $(OBJDIR)/native-config.tmp $(OBJDIR)/aerial-idle.service
	rm -f $(OBJDIR)/aerial.service $(OBJDIR)/org.aeriallock.Aerial1.service
	rm -f companion/aerial $(COMPANION_OBJS) $(OBJDIR)/local-config.o $(OBJDIR)/local-config.moc $(OBJDIR)/config-repository.moc $(OBJDIR)/service.moc $(OBJDIR)/companion_adaptor.cpp $(OBJDIR)/companion_adaptor.h $(OBJDIR)/moc_companion_adaptor.cpp
	rm -f companion/aerial-media-probe $(CONTENT_OBJS) $(PROBE_OBJS)
	rm -f cli/aerialctl $(CLI_OBJS) $(OBJDIR)/aerialctl.moc $(OBJDIR)/job-object.moc $(OBJDIR)/job_adaptor.cpp $(OBJDIR)/job_adaptor.h $(OBJDIR)/moc_job_adaptor.cpp $(OBJDIR)/companion_client.o $(OBJDIR)/companion_client.cpp $(OBJDIR)/companion_client.h $(OBJDIR)/moc_companion_client.cpp
	rm -f $(OBJDIR)/local-content.o $(OBJDIR)/local-content.moc
	rm -f $(OBJDIR)/companion-hint.o $(OBJDIR)/companion-client.moc
	rm -f $(addprefix $(OBJDIR)/,http-operation.moc manifest-store.moc local-probe.moc transfer.moc transfer-queue.moc policy-monitor.moc)
	@rmdir $(OBJDIR) 2>/dev/null || true

dev: DEBUG_DISMISS=1
dev: DEBUG_VERBOSE=1
dev: AUTH_HELPER=$(CURDIR)/native/aerial-lock-auth
dev: EMERGENCY_CLIENT=$(CURDIR)/emergency/aerial-lock-emergency
dev: all
	bash Scripts/dev/run.sh
