bin_PROGRAMS = \
	ioctl-test		\
	sliced-vbi-test		\
	sliced-vbi-detect	\
	vbi-test 		\
	v4lgrab			\
	v4l2grab		\
	driver-test		\
	stress-buffer		\
	capture-example

if HAVE_X11
bin_PROGRAMS += pixfmt-test
endif

driver_test_SOURCES = driver-test.c
driver_test_LDADD = ../../utils/libv4l2util/libv4l2util.la

pixfmt_test_SOURCES = pixfmt-test.c
pixfmt_test_CFLAGS = $(X11_CFLAGS)
pixfmt_test_LDFLAGS = $(X11_LIBS)

v4l2grab_SOURCES = v4l2grab.c
v4l2grab_LDADD = ../../lib/libv4l2/libv4l2.la

ioctl_test_SOURCES = ioctl-test.c

sliced_vbi_test_SOURCES = sliced-vbi-test.c

sliced_vbi_detect_SOURCES = sliced-vbi-detect.c

vbi_test_SOURCES = vbi-test.c

v4lgrab_SOURCES = v4lgrab.c

stress_buffer_SOURCES = stress-buffer.c

capture_example_SOURCES = capture-example.c
