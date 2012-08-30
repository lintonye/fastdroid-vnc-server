# Linux Makefile

LOCAL_PATH := .
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
	external/zlib \
	external/jpeg

LOCAL_SHARED_LIBRARIES := -lz -lpthread
LIBJPEG_PATH := /usr/lib
LOCAL_STATIC_LIBRARIES := $(LIBJPEG_PATH)/libjpeg.a 

LOCAL_MODULE:= fastdroid-vnc

# build 

GCC := gcc
LD := gcc

LOCAL_OBJ_FILES := $(subst .c,.o,$(LOCAL_SRC_FILES))
C_INCLUDES := $(addprefix -I,$(LOCAL_C_INCLUDES))

all: $(LOCAL_MODULE)

$(LOCAL_MODULE): $(LOCAL_OBJ_FILES)
	$(LD) -o $@ $(LOCAL_OBJ_FILES) $(C_LIBRARIES) $(LOCAL_STATIC_LIBRARIES) $(LOCAL_SHARED_LIBRARIES)

$(LOCAL_OBJ_FILES): %.o:%.c
	$(GCC) -c -o $@ $< $(C_INCLUDES)

clean:
	rm -rf $(LOCAL_OBJ_FILES) $(LOCAL_MODULE)
