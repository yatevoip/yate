yate_library(yateasn yasn SHARED
    yasn/asn.cpp
)
yate_library(yatesip ysip STATIC
    ysip/util.cpp
    ysip/engine.cpp
    ysip/message.cpp
    ysip/transaction.cpp
)
yate_library(yatertp yrtp STATIC
    yrtp/transport.cpp
    yrtp/session.cpp
    yrtp/secure.cpp
    yrtp/dejitter.cpp
)
yate_library(yatesdp ysdp STATIC
    ysdp/media.cpp
    ysdp/session.cpp
    ysdp/parser.cpp
)
yate_library(yateiax yiax STATIC
    yiax/frame.cpp
    yiax/engine.cpp
    yiax/transaction.cpp
)
yate_library(yatejabber yjabber SHARED
    yjabber/xmpputils.cpp
    yjabber/jbstream.cpp
    yjabber/jbengine.cpp
    yjabber/session.cpp
    yjabber/jgengine.cpp
)
yate_library(yatescript yscript SHARED
    yscript/evaluator.cpp
    yscript/script.cpp
    yscript/javascript.cpp
    yscript/jsobjects.cpp
)
yate_library(yatemgcp ymgcp SHARED
    ymgcp/engine.cpp
    ymgcp/transaction.cpp
    ymgcp/message.cpp
    ymgcp/endpoint.cpp
)
yate_library(yatemodem ymodem STATIC
    ymodem/modem.cpp
    ymodem/uart.cpp
)
yate_library(yatepbx ypbx STATIC
    ypbx/multiroute.cpp
    ypbx/assist.cpp
)
yate_library(yatesig ysig SHARED
    ysig/engine.cpp
    ysig/address.cpp
    ysig/sigcall.cpp
    ysig/sigtran.cpp
    ysig/interface.cpp
    ysig/layer2.cpp
    ysig/layer3.cpp
    ysig/layer4.cpp
    ysig/router.cpp
    ysig/sccp.cpp
    ysig/tcap.cpp
    ysig/dumper.cpp
    ysig/management.cpp
    ysig/isup.cpp
    ysig/tup.cpp
    ysig/testpart.cpp
    ysig/q921.cpp
    ysig/q931.cpp
)
yate_library(ysnmp ysnmp STATIC
    ysnmp/snmp.cpp
)
yate_library(yateradio yradio SHARED
    yradio/codec.cpp
    yradio/radio.cpp
)
target_link_libraries(yatesig PUBLIC yateasn)
target_link_libraries(ysnmp PUBLIC yateasn)
if(YATE_HAVE_ILBC)
    add_subdirectory(ilbc)
endif()
if(YATE_HAVE_ILBC_WEBRTC OR YATE_HAVE_ISAC OR YATE_HAVE_G722_WEBRTC)
    add_subdirectory(miniwebrtc)
endif()
if(YATE_BUILD_TEST_MODULES)
    add_executable(yate-ss7test ysig/main-ss7test.cpp)
    target_link_libraries(yate-ss7test PRIVATE yatesig yate_build_options)
endif()
