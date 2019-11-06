/**
 * g722webrtc.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * G.722 codec using library based on WebRTC project.
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2019 Null Team
 *
 * This software is distributed under multiple licenses;
 * see the COPYING file in the main directory for licensing
 * information for this specific distribution.
 *
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 *
 * Copyright (c) 2011, The WebRTC project authors. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 *  * Neither the name of Google nor the names of its contributors may
 *    be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <yatephone.h>

extern "C" {
#include "signal_processing_library.h"
#include "g722_interface.h"
}

// G.722 was erroneously declared as 8000 samples/s but it's really 16000
// Minimum frame is 10ms (80 octets) but we will make all calculations for 20
#define G722_SAMPL 320
#define G722_SAMP8 160
#define G722_BLOCK 640
#define G722_FRAME 160

using namespace TelEngine;
namespace { // anonymous

class G722Codec : public DataTranslator
{
public:
    G722Codec(const char* sFormat, const char* dFormat, bool encoding);
    ~G722Codec();
    virtual bool valid() const
	{ return m_enc || m_dec; }
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp,
	unsigned long flags);
private:
    bool m_encoding;                     // Encoder/decoder flag
    G722EncInst* m_enc;                  // Encoder instance
    G722DecInst* m_dec;                  // Decoder instance

    DataBlock m_data;                    // Incomplete input data
    DataBlock m_outdata;                 // Codec output
};

class G722Factory : public TranslatorFactory
{
public:
    G722Factory(const TranslatorCaps* caps);
    virtual const TranslatorCaps* getCapabilities() const
	{ return m_caps; }
    virtual DataTranslator* create(const DataFormat& sFormat, const DataFormat& dFormat);
private:
    const TranslatorCaps* m_caps;
};

class G722Module : public Module
{
public:
    G722Module();
    ~G722Module();
    inline void incCount() {
	    Lock mylock(this);
	    m_count++;
	}
    inline void decCount() {
	    Lock mylock(this);
	    m_count--;
	}
    virtual void initialize();
    virtual bool isBusy() const
	{ return (m_count != 0); }
protected:
    virtual void statusParams(String& str);
private:
    int m_count;                         // Current number of codecs
    G722Factory* m_g722;                 // Factory used to create codecs
};


INIT_PLUGIN(G722Module);

static TranslatorCaps s_caps[] = {
    { 0, 0 },
    { 0, 0 },
    { 0, 0 }
};

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow)
	return !__plugin.isBusy();
    return true;
}


/*
 * G722Codec
 */
G722Codec::G722Codec(const char* sFormat, const char* dFormat, bool encoding)
    : DataTranslator(sFormat,dFormat), m_encoding(encoding),
    m_enc(0), m_dec(0)
{
    Debug(&__plugin,DebugAll,"G722Codec(\"%s\",\"%s\",%scoding) [%p]",
	sFormat,dFormat,m_encoding ? "en" : "de",this);
    __plugin.incCount();
    if (encoding) {
	::WebRtcG722_CreateEncoder(&m_enc);
	::WebRtcG722_EncoderInit(m_enc);
    }
    else {
	::WebRtcG722_CreateDecoder(&m_dec);
	::WebRtcG722_DecoderInit(m_dec);
    }
}

G722Codec::~G722Codec()
{
    if (m_enc)
	::WebRtcG722_FreeEncoder(m_enc);
    if (m_dec)
	::WebRtcG722_FreeDecoder(m_dec);
    Debug(&__plugin,DebugAll,
	"G722Codec(%scoding) destroyed [%p]",m_encoding ? "en" : "de",this);
    __plugin.decCount();
}

unsigned long G722Codec::Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
{
    RefPointer<DataSource> src = getTransSource();
    if (!(src && valid()))
	return 0;
    if (data.null() && (flags & DataSilent))
	return src->Forward(data,tStamp,flags);
    ref();
    if (m_encoding && (tStamp != invalidStamp()) && !m_data.null())
	tStamp -= (m_data.length() / 2);
    m_data += data;
    int frames,consumed;
    // G.722 declared rate and timestamps are for 8000 samples/s so it needs tweaking
    if (m_encoding) {
	tStamp /= 2;
	frames = m_data.length() / G722_BLOCK;
	consumed = frames * G722_BLOCK;
	if (frames) {
	    m_outdata.resize(frames * G722_FRAME);
	    uint8_t* d = (uint8_t*)m_outdata.data();
	    const int16_t* s = (const int16_t*)m_data.data();
	    for (int i=0; i<frames; i++) {
		::WebRtcG722_Encode(m_enc,s,G722_SAMPL,d);
		s += G722_SAMPL;
		d += G722_FRAME;
	    }
	}
    }
    else {
	tStamp *= 2;
	frames = m_data.length() / G722_FRAME;
	consumed = frames * G722_FRAME;
	if (frames) {
	    m_outdata.resize(frames * G722_BLOCK);
	    int16_t* d = (int16_t*)m_outdata.data();
	    const uint8_t* s = (const uint8_t*)m_data.data();
	    int16_t t = G722_WEBRTC_SPEECH;
	    for (int i=0; i<frames; i++) {
		// encoded data gets casted to uint8_t and then const uint8_t
		::WebRtcG722_Decode(m_dec,(int16_t*)s,G722_FRAME,d,&t);
		s += G722_FRAME;
		d += G722_SAMPL;
	    }
	}
    }
    if (!tStamp)
	tStamp = timeStamp() + (frames * G722_SAMP8);
    XDebug("G722Codec",DebugAll,"%scoding %d frames of %d input bytes (consumed %d) in %d output bytes",
	m_encoding ? "en" : "de",frames,m_data.length(),consumed,m_outdata.length());
    unsigned long len = 0;
    if (frames) {
	m_data.cut(-consumed);
	len = getTransSource()->Forward(m_outdata,tStamp,flags);
    }
    deref();
    return len;
}


/*
 * G722Factory
 */
G722Factory::G722Factory(const TranslatorCaps* caps)
    : TranslatorFactory("g722"),
    m_caps(caps)
{
}

DataTranslator* G722Factory::create(const DataFormat& sFormat, const DataFormat& dFormat)
{
    if ((sFormat == "slin/16000") && (dFormat == "g722/16000"))
	return new G722Codec(sFormat,dFormat,true);
    else if ((sFormat == "g722/16000") && (dFormat == "slin/16000"))
	return new G722Codec(sFormat,dFormat,false);
    return 0;
}


/*
 * G722Module
 */
G722Module::G722Module()
    : Module("g722webrtc","misc"),
    m_count(0), m_g722(0)
{
    char ver[24] = {0};
    ::WebRtcG722_Version(ver,sizeof(ver));
    Output("Loaded module G722 - based on WebRTC G.722 library version %s",ver);

    const FormatInfo* f = FormatRepository::addFormat("g722/16000",160,20000,"audio",16000);
    s_caps[0].src = s_caps[1].dest = f;
    s_caps[0].dest = s_caps[1].src = FormatRepository::getFormat("slin/16000");
    // FIXME: put proper conversion costs
    s_caps[0].cost = s_caps[1].cost = 5;
    m_g722 = new G722Factory(s_caps);
}

G722Module::~G722Module()
{
    Output("Unloading module G722 with %d codecs still in use",m_count);
    TelEngine::destruct(m_g722);
}

void G722Module::initialize()
{
    static bool s_first = true;
    Output("Initializing module G722");
    if (s_first) {
	installRelay(Level);
	installRelay(Status);
	installRelay(Command);
	s_first = false;
    }
}

void G722Module::statusParams(String& str)
{
    str << "codecs=" << m_count;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
