# The image mapper. `$(OUT)/test_o2map <image>...` also decodes real carts.
# test_o2app: protocol 2 -- APPBANK recognition and the o2app.h decode.
# test_fujicart: the cart's staged image, its mailbox bytes and the swap.
# test_o2mail: the mailbox service's timeouts, the mount reset and the percent.
TESTS = test_o2map test_o2app test_fujicart test_o2mail
test_o2map_SRCS = $(SRC)/o2map.c
test_o2app_SRCS = $(SRC)/o2map.c
test_fujicart_SRCS = $(SRC)/fuji_cart.c
test_o2mail_SRCS = $(SRC)/fujimail.c

include ../../../common/host_test/cart.mk
