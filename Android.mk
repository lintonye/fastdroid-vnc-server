LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LIB_VNC_ROOT := LibVNCServer-0.9.7
LIB_VNC_SVR_PATH := $(LIB_VNC_ROOT)/libvncserver
LIB_VNC_SVR_SRC := \
	main.c \
	rfbserver.c \
	rfbregion.c \
	auth.c \
	sockets.c \
	stats.c \
	corre.c \
	hextile.c \
	rre.c \
	translate.c \
	cutpaste.c \
	httpd.c \
	cursor.c \
	font.c \
	draw.c \
	selbox.c \
	d3des.c \
	vncauth.c \
	cargs.c \
	minilzo.c \
	ultra.c \
	scale.c \
	zlib.c \
	zrle.c \
	zrleoutstream.c \
	zrlepalettehelper.c \
	zywrletemplate.c \
	tight.c

LOCAL_SRC_FILES := \
	fbvncserver.c \
	$(addprefix $(LIB_VNC_SVR_PATH)/,$(LIB_VNC_SVR_SRC))

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH) \
	$(LOCAL_PATH)/$(LIB_VNC_SVR_PATH) \
	$(LOCAL_PATH)/$(LIB_VNC_ROOT) \
	$(LOCAL_PATH)/external/jpeg/jpeg \
	external/zlib \
	external/jpeg

LOCAL_LDLIBS += -lz

#LOCAL_SHARED_LIBRARIES := libz
LOCAL_STATIC_LIBRARIES := libjpeg

LOCAL_MODULE:= fvnc

include $(BUILD_EXECUTABLE)
