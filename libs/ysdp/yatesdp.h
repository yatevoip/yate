/**
 * yatesdp.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * SDP media handling
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2023 Null Team
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
 */

#ifndef __YATESDP_H
#define __YATESDP_H

#ifndef __cplusplus
#error C++ is required
#endif

#include <yatemime.h>
#include <yatephone.h>

#ifdef _WINDOWS

#ifdef LIBYSDP_EXPORTS
#define YSDP_API __declspec(dllexport)
#else
#ifndef LIBYSDP_STATIC
#define YSDP_API __declspec(dllimport)
#endif
#endif

#endif /* _WINDOWS */

#ifndef YSDP_API
#define YSDP_API
#endif

/**
 * Holds all Telephony Engine related classes.
 */
namespace TelEngine {

class SDPMedia;
class SDPSession;
class SDPParser;

/**
 * This class holds RFC 2833 payloads for known audio rates
 * @short RFC 2833 payloads for known audio rates
 */
class YSDP_API Rfc2833
{
public:
    /**
     * Suported (known) rates
     */
    enum Rate {
	Rate8khz = 0,
	Rate16khz,
	Rate32khz,
	RateCount,
    };

    /**
     * Constructor
     */
    inline Rfc2833()
	{ set(); }

    /**
     * Check if a payload is present in the list
     * @param payload Payload to check
     */
    inline bool includes(int payload) const {
	    for (int i = 0; i < RateCount; ++i)
		if (m_payloads[i] == payload)
		    return true;
	    return false;
	}
	
    /**
     * Select RFC 2833 payload for given media format
     * @param fmt Format internal name
     * @return Payload value, negative if not available
     */
    inline int payload(const String& fmt) const {
	    int r = fmtRate(fmt);
	    return r < RateCount ? m_payloads[r] : -1;
	}

    /**
     * Replace all values
     * @param other Optional values to replace with. Reset if not present
     */
    inline void set(const Rfc2833* other = 0) {
	    for (int i = 0; i < RateCount; ++i)
		m_payloads[i] = other ? (*other)[i] : -1;
	}

    /**
     * Update payloads
     * @param params Parameters list
     * @param defaults Payload list with default values
     * @param force True to force updating if a needed parameter is not present
     * @param param Parameter name. defaults to "rfc2833"
     */
    void update(const NamedList& params, const Rfc2833& defaults, bool force = true,
	const String& param = String::empty());

    /**
     * Update payload for specific rate
     * @param value Payload
     * @param rate Rate value
     * @param defaults Payload list with default values
     */
    void update(int rate, const String& value, const Rfc2833& defaults);

    /**
     * Put RFC 2833 parameters in a parameter list
     * @param params Destination list
     * @param param Parameter name. Defaults to "rtp_rfc2833"
     */
    void put(NamedList& params, const String& param = String::empty()) const;

    inline int operator[](int index) const
	{ return m_payloads[index]; }

    inline int& operator[](int index)
	{ return m_payloads[index]; }

    /**
     * Assignment operator
     */
    inline Rfc2833& operator=(const Rfc2833& other)
	{ set(&other); return *this; }

    /**
     * Dump (append) payloads to destination string
     * Format: rate=payload[rate1=payload1...],
     * @param buf Destination string
     * @return Destination string
     */
    String& dump(String& buf) const;

    /**
     * Select RFC 2833 rate for given media format
     * @param fmt Format internal name
     * @return Rate value as enumeration
     */
    static int fmtRate(const String& fmt);

    /**
     * Retrieve rate enumeration value from rate value
     * @param s Rate value
     * @return Rate as enumeration, RateCount if not found
     */
    static inline int rate(const String& s) {
	    for (int r = 0; r < RateCount; ++r)
		if (s_rates[r] == s)
		    return r;
	    return RateCount;
	}

    /**
     * Retrieve rate value from rate enumeration value
     * @param index Rate enumeration index
     * @return Rate name, empty string if not found
     */
    static inline const String& rateValue(int index)
	{ return index < RateCount ? s_rates[index] : String::empty(); }

protected:
    static const String s_rates[RateCount];

    int m_payloads[RateCount];
};


/**
 * This class holds a single SDP media description
 * @short SDP media description
 */
class YSDP_API SDPMedia : public NamedList
{
public:
    /**
     * RTP session/media direction
     */
    enum Direction {
	DirUnknown = 0,
	DirRecv = 1,
	DirSend = 2,
	DirBidir = 3,
	DirInactive = 4,
    };

    /**
     * Constructor
     * @param media Media type name
     * @param transport Transport name
     * @param formats Comma separated list of formats
     * @param rport Optional remote media port
     * @param lport Optional local media port
     */
    SDPMedia(const char* media, const char* transport, const char* formats,
	int rport = -1, int lport = -1);

    /**
     * Destructor
     */
    virtual ~SDPMedia();

    /**
     * Check if this media type is audio
     * @return True if this media describe an audio one
     */
    inline bool isAudio() const
	{ return m_audio; }

    /**
     * Check if this media type is video
     * @return True if this media describe a video one
     */
    inline bool isVideo() const
	{ return m_video; }

    /**
     * Check if a media parameter changed
     * @return True if a media changed
     */
    inline bool isModified() const
	{ return m_modified; }

    /**
     * Set or reset media parameter changed flag
     * @param modified The new value of the media parameter changed flag
     */
    inline void setModified(bool modified = true)
	{ m_modified = modified; }

    /**
     * Retrieve the media suffix (built from type)
     * @return Media suffix
     */
    inline const String& suffix() const
	{ return m_suffix; }

    /**
     * Check if media is started
     * @return True if started, false otherwise
     */
    inline bool isStarted() const
	{ return m_id && m_transport && m_format && m_lPort; }

    /**
     * Retrieve the media transport name
     * @return The media transport name
     */
    inline const String& transport() const
	{ return m_transport; }

    /**
     * Retrieve the media id
     * @return The media id
     */
    inline const String& id() const
	{ return m_id; }

    /**
     * Retrieve the current media format
     * @return The current media format
     */
    inline const String& format() const
	{ return m_format; }

    /**
     * Retrieve the formats set for this media
     * @return Comma separated list of media formats
     */
    inline const String& formats() const
	{ return m_formats; }

    /**
     * Retrieve the remote media port
     * @return The remote media port
     */
    inline const String& remotePort() const
	{ return m_rPort; }

    /**
     * Retrieve the local media port
     * @return The local media port
     */
    inline const String& localPort() const
	{ return m_lPort; }

    /**
     * Retrieve rtp payload mappings
     * @return Rtp payload mappings
     */
    inline const String& mappings() const
	{ return m_mappings; }

    /**
     * Set rtp payload mappings for this media
     * @param newMap New rtp payload mappings
     */
    inline void mappings(const char* newMap)
	{ if (newMap) m_mappings = newMap; }

    /**
     * Retrieve RFC 2833 payloads of this media
     * @return RFC 2833 payloads of this media
     */
    inline const Rfc2833& rfc2833() const
	{ return m_rfc2833; }

    /**
     * Set RFC 2833 payloads of this media
     * @param values SDP RFC 2833 payloads to set
     */
    inline void rfc2833(const Rfc2833& values)
	{ m_rfc2833 = values; }

    /**
     * Check if RFC 2833 was selected for this media
     * @return True if RFC 2833 was selected for this media, false otherwise
     */
    inline bool haveRfc2833() const
	{ return m_haveRfc3833; }

    /**
     * Select RFC 2833 payload for given media format
     * @param fmt Format internal name
     * @return Payload value, negative if not available
     */
    inline int selectRfc2833(const String& fmt) {
	    int rVal = m_rfc2833.payload(fmt);
	    m_haveRfc3833 = (rVal >= 0);
	    return rVal;
	}

    /**
     * Retrieve remote crypto description
     * @return Remote crypto description
     */
    inline const String& remoteCrypto() const
	{ return m_rCrypto; }

    /**
     * Retrieve local crypto description
     * @return Local crypto description
     */
    inline const String& localCrypto() const
	{ return m_lCrypto; }

    /**
     * Check if this media is securable
     * @return True if this media is securable
     */
    inline bool securable() const
	{ return m_securable; }

    /**
     * Compare this media with another one
     * @param other The media to compare with
     * @param ignorePort Ignore differences caused only by port number
     * @param checkStarted Check started related parameters: true when media should be kept if started and matched
     * @return True if both media have the same formats, transport and remote port
     */
    bool sameAs(const SDPMedia* other, bool ignorePort = false, bool checkStarted = false) const;

    /**
     * Check if local part of this media changed
     * @return True if local part of this media changed
     */
    inline bool localChanged() const
	{ return m_localChanged; }

    /**
     * Set or reset local media changed flag
     * @param chg The new value for local media changed flag
     */
    inline void setLocalChanged(bool chg = false)
	{ m_localChanged = chg; }

    /**
     * Retrieve a formats list from this media
     * @return Comma separated list of media formats (from formats list,
     *  current format or a default G711, 'alaw,mulaw', list
     */
    const char* fmtList() const;

    /**
     * Update this media from formats and ports
     * @param formats New media formats
     * @param rport Optional remote media port
     * @param lport Optional local media port
     * @param force Force updating formats even if incompatible with old ones
     * @return True if media changed
     */
    bool update(const char* formats, int rport = -1, int lport = -1, bool force = false);

    /**
     * Update from a chan.rtp message (rtp id and local port)
     * @param msg The list of parameters
     * @param pickFormat True to update media format(s) from the list
     */
    void update(const NamedList& msg, bool pickFormat);

    /**
     * Add or replace a parameter by name and value, set the modified flag
     * @param name Parameter name
     * @param value Parameter value
     * @param append True to append, false to replace
     */
    void parameter(const char* name, const char* value, bool append);

    /**
     * Add or replace a parameter, set the modified flag
     * @param param The parameter
     * @param append True to append, false to replace
     */
    void parameter(NamedString* param, bool append);

    /**
     * Set a new crypto description, set the modified flag if changed.
     * Reset the media securable flag if the remote crypto is empty
     * @param desc The new crypto description
     * @param remote True to set the remote crypto, false to set the local one
     */
    void crypto(const char* desc, bool remote);

    /**
     * Set media direction
     * @param value New direction
     * @param remote True to set the remote direction, false to set the local one
     */
    void direction(int value, bool remote);

    /**
     * Retrieve negotiated media direction to be sent to remote
     * @param sessLDir SDP session level local direction, if known
     * @return Media direction
     */
    int direction(int sessLDir = 0);

    /**
     * Put this net media in a parameter list
     * @param msg Destination list
     * @param putPort True to add remote media port
     */
    void putMedia(NamedList& msg, bool putPort = true);

    /**
     * Copy RTP related data from old media
     * @param other Source media
     */
    void keepRtp(const SDPMedia& other);

    /**
     * Set data used in debug
     * @param enabler The DebugEnabler to use (0 to to use the parser)
     * @param ptr Pointer to print, 0 to use the media pointer
     * @param traceId Trace ID to use for debugging
     */
    void setSdpDebug(DebugEnabler* enabler = 0, void* ptr = 0, const String* traceId = 0);

    /**
     * Retrieve direction if known
     * @param dir Destination variable to set the direction in
     * @param name Direction name
     */
    static inline void setDirection(int& dir, const char* name) {
	int d = lookup(name,s_sdpDir);
	if (d != DirUnknown)
	    dir = d;
    }

    /**
     * Retrieve format mapping to payload
     * @param mappings Mappings list
     * @param fmt Format name
     * @return -1:incorrect mapping value, -2:format not found, payload number otherwise (>=0)
     */
    static int payloadMapping(const String& mappings, const String& fmt);

    /**
     * SDP media direction dictionary
     */
    static const TokenDict s_sdpDir[];

private:
    bool m_audio;
    bool m_video;
    bool m_modified;
    bool m_securable;
    bool m_haveRfc3833;
    // local rtp data changed flag
    bool m_localChanged;
    // suffix used for this type
    String m_suffix;
    // transport protocol
    String m_transport;
    // list of supported format names
    String m_formats;
    // format used for sending data
    String m_format;
    // id of the local media channel
    String m_id;
    // remote media port
    String m_rPort;
    // mappings of RTP payloads
    String m_mappings;
    // local media port
    String m_lPort;
    // payloads for telephone/event
    Rfc2833 m_rfc2833;
    // remote security descriptor
    String m_rCrypto;
    // local security descriptor
    String m_lCrypto;
    // Local / remote media direction
    int m_lDir;
    int m_rDir;
    DebugEnabler* m_enabler;             // Debug enabler used for output
    void* m_ptr;                         // Pointer to show in debug messages
    String m_traceId;
};


/**
 * This class holds RTP/SDP data for multiple media types
 * NOTE: The SDPParser pointer held by this class is assumed to be non NULL
 * @short A holder for a SDP session
 */
class YSDP_API SDPSession
{
public:
    /**
     * RTP media status enumeration
     */
    enum {
	MediaMissing,
	MediaStarted,
	MediaMuted
    };

    /**
     * RTP media status enumeration
     */
    enum PasstroughLocation {
	PasstroughNone = 0,
	PasstroughProvisional,
	PasstroughAnswer,
	PasstroughAck,                   // Outgoing transaction ACK
	PasstroughUpdate,                // Session update
    };

    /**
     * Constructor
     * @param parser The SDP parser whose data this object will use
     */
    SDPSession(SDPParser* parser);

    /**
     * Constructor
     * @param parser The SDP parser whose data this object will use
     * @param params SDP session parameters
     */
    SDPSession(SDPParser* parser, NamedList& params);

    /**
     * Destructor. Reset the object
     */
    virtual ~SDPSession();

    /**
     * Get RTP local host
     * @return RTP local host
     */
    inline const String& getHost() const
	{ return m_host; }

    /**
     * Get local RTP address
     * @return Local RTP address (external or local)
     */
    inline const String& getRtpAddr() const
	{ return m_externalAddr ? m_externalAddr : m_rtpLocalAddr; }

    /**
     * Set a new media list
     * @param media New media list
     * @param preserveExisting Try to preserve existing started media
     * @return True if media changed
     */
    bool setMedia(ObjList* media, bool preserveExisting = false);

    /**
     * Put specified media parameters into a list of parameters
     * @param msg Destination list
     * @param media List of SDP media information
     * @param putPort True to add the media port
     * @param sessParams Optional session level SDP parameters
     */
    static void putMedia(NamedList& msg, ObjList* media, bool putPort = true,
	const NamedList* sessParams = 0);

    /**
     * Put session media parameters into a list of parameters
     * @param msg Destination list
     * @param putPort True to add the media port
     */
    inline void putMedia(NamedList& msg, bool putPort = true)
	{ putMedia(msg,m_rtpMedia,putPort,m_parsedParams); }

    /**
     * Retrieve a single media description
     * @param name Name of the media to retrieve
     * @return Pointer to media descriptor, NULL if no such media set
     */
    SDPMedia* getMedia(const String& name) const
	{ return m_rtpMedia ? static_cast<SDPMedia*>((*m_rtpMedia)[name]) : 0; }

    /**
     * Update the RFC 2833 availability and payload
     * @param value String to get payload or availability
     * @param rate Rate to set
     */
    void setRfc2833(const String& value, int rate = Rfc2833::Rate8khz);

    /**
     * Update the RFC 2833 availability and payload
     * @param params Parameters list
     * @param force Force update even if needed parameters are not present
     */
    void setRfc2833(const NamedList& params, bool force);

    /**
     * Build and dispatch a chan.rtp message for a given media. Update media on success
     * @param media The media to use
     * @param addr Remote RTP address
     * @param start True to request RTP start
     * @param pick True to update local parameters (other then media) from returned message
     * @param context Pointer to user provided context, optional
     * @return True if the message was succesfully handled
     */
    bool dispatchRtp(SDPMedia* media, const char* addr,	bool start, bool pick, RefObject* context = 0);

    /**
     * Calls dispatchRtp() for each media in the list
     * Update it on success. Remove it on failure
     * @param addr Remote RTP address
     * @param start True to request RTP start
     * @param context Pointer to user provided context, optional
     * @return True if the message was succesfully handled for at least one media
     */
    bool dispatchRtp(const char* addr, bool start, RefObject* context = 0);

    /**
     * Try to start RTP (calls dispatchRtp()) for each media in the list
     * @param context Pointer to user provided context, optional
     * @return True if at least one media was started
     */
    bool startRtp(RefObject* context = 0);

    /**
     * Update from parameters and optionally build a default SDP.
     * @param params List of parameters to update from
     * @param defaults Build a default SDP from parser formats if no media is found in params
     * @return True if media changed
     */
    bool updateSDP(const NamedList& params, bool defaults = true);

    /**
     * Update RTP/SDP data from parameters
     * @param params List of parameters to update from
     * @return True if media or local address changed
     */
    bool updateRtpSDP(const NamedList& params);

    /**
     * Creates a SDP body from transport address and list of media descriptors
     * @param addr The address to set. Use own host if empty
     * @param mediaList Optional media list. Use own list if the given one is 0
     * @return MimeSdpBody pointer or 0 if there is no media to set
     */
    MimeSdpBody* createSDP(const char* addr, ObjList* mediaList = 0);

    /**
     * Creates a SDP body for current media status
     * @return MimeSdpBody pointer or 0 if media is missing
     */
    MimeSdpBody* createSDP();

    /**
     * Creates a SDP from RTP address data present in message.
     * Use the raw SDP if present.
     * @param loc Location (session state)
     * @param msg The list of parameters
     * @param update True to update RTP/SDP data if raw SDP is not found in the list
     * @param allowEmptyAddr Allow empty address in parameters (default: false)
     * @return MimeSdpBody pointer or 0
     */
    MimeSdpBody* createPasstroughSDP(int loc, NamedList& msg, bool update = true,
	bool allowEmptyAddr = false);

    /**
     * Creates a set of unstarted external RTP channels from remote addr and
     *  builds SDP from them
     * @param addr Remote RTP address used when dispatching the chan.rtp message
     * @param msg List of parameters used to update data
     * @return MimeSdpBody pointer or 0
     */
    inline MimeSdpBody* createRtpSDP(const char* addr, const NamedList& msg)
	{ updateSDP(msg); return createRtpSDP(addr,false); }

    /**
     * Creates a set of RTP channels from address and media info and builds SDP from them
     * @param addr Remote RTP address used when dispatching the chan.rtp message
     * @param start True to create a started RTP
     * @return MimeSdpBody pointer or 0
     */
    inline MimeSdpBody* createRtpSDP(const char* addr, bool start)
	{ return dispatchRtp(addr,start) ? createSDP(getRtpAddr()) : 0; }

    /**
     * Creates a set of started external RTP channels from remote addr and
     *  builds SDP from them
     * @param start True to create a started RTP
     * @return MimeSdpBody pointer or 0
     */
    inline MimeSdpBody* createRtpSDP(bool start)
	{
	    if (m_rtpAddr.null()) {
		m_mediaStatus = MediaMuted;
		return createSDP(0);
	    }
	    return createRtpSDP(m_rtpAddr,start);
	}

    /**
     * Update media format lists from parameters
     * @param msg Parameter list
     * @param changeMedia True to update media list if required
     */
    void updateFormats(const NamedList& msg, bool changeMedia = false);

    /**
     * Add raw SDP forwarding parameter from body if SDP forward is enabled
     * @param msg Destination list
     * @param body Mime body to process
     * @return True if the parameter was added
     */
    bool addSdpParams(NamedList& msg, const MimeBody* body);

    /**
     * Add raw SDP forwarding parameter if SDP forward is enabled
     * @param msg Destination list
     * @param rawSdp The raw sdp content
     * @return True if the parameter was added
     */
    bool addSdpParams(NamedList& msg, const String& rawSdp);

    /**
     * Add RTP forwarding parameters to a message (media and address)
     * @param msg Destination list
     * @param natAddr Optional NAT address if detected
     * @param body Pointer to the body to extract raw SDP from
     * @param force True to override RTP forward flag
     * @param allowEmptyAddr Allow empty address in parameters (default: false)
     * @return True if RTP data was added. Media is always added if present and
     *  remote address is not empty
     */
    bool addRtpParams(NamedList& msg, const String& natAddr = String::empty(),
	const MimeBody* body = 0, bool force = false, bool allowEmptyAddr = false);

    /**
     * Reset this object to default values
     * @param all True to reset all parameters including configuration
     */
    virtual void resetSdp(bool all = true);

    /**
     * Build a chan.rtp message and populate with media information
     * @param media The media list
     * @param addr Remote RTP address
     * @param start True to request RTP start
     * @param context Pointer to reference counted user provided context
     * @return The message with media information, NULL if media or addr are missing
     */
    virtual Message* buildChanRtp(SDPMedia* media, const char* addr, bool start, RefObject* context);

    /**
     * Build a chan.rtp message without media information
     * @param context Pointer to reference counted user provided context
     * @return The message with user data set but no media information
     */
    virtual Message* buildChanRtp(RefObject* context) = 0;

    /**
     * Check if local RTP data changed for at least one media
     * @return True if local RTP data changed for at least one media
     */
    bool localRtpChanged() const;

    /**
     * Set or reset the local RTP data changed flag for all media
     * @param chg The new value for local RTP data changed flag of all media
     */
    void setLocalRtpChanged(bool chg = false);

    /**
     * Update RTP/SDP data from parameters
     * @param params Parameter list
     * @param rtpAddr String to be filled with rtp address from the list
     * @param oldList Optional existing media list (found media will be removed
     *  from it and added to the returned list
     * @param allowEmptyAddr Allow empty address in parameters (default: false)
     * @return List of media or 0 if not found or rtpAddr is empty
     */
    static ObjList* updateRtpSDP(const NamedList& params, String& rtpAddr,
	ObjList* oldList = 0, bool allowEmptyAddr = false);

protected:
    /**
     * Media changed notification.
     * This method is called when setting new media and an old one changed
     * @param media Old media that changed
     */
    virtual void mediaChanged(const SDPMedia& media);

    /**
     * Dispatch rtp notification.
     * This method is called before dispatching the message.
     * Clear the message to stop dispatch
     * @param msg Message to dispatch
     * @param media Media for which the message is going to be dispatched
     */
    virtual void dispatchingRtp(Message*& msg, SDPMedia* media);

    /**
     * Set data used in debug
     * @param enabler The DebugEnabler to use (0 to to use the parser)
     * @param ptr Pointer to print, 0 to use the session pointer
     * @param traceId Trace ID to use for debugging
     */
    void setSdpDebug(DebugEnabler* enabler = 0, void* ptr = 0, const String& traceId = String::empty());

    /**
     * Print current media to output
     * @param reason Reason to print
     */
    void printRtpMedia(const char* reason);

    /**
     * Set extra parameters for formats
     * @param list List of parameters
     * @param out True if session is outgoing, false otherwise
     */
    void setFormatsExtra(const NamedList& list, bool out);

    /**
     * Parse a received SDP body, process session level parameters
     * @param sdp Pointer to received SDP body
     * @return List with handled session level parameters, may be NULL
     */
    NamedList* parseSessionParams(const MimeSdpBody* sdp);

    /**
     * Replace parsed SDP session level parameters
     * Consume receveived pointer
     * @param params Parsed SDP session params
     */
    inline void setSessionParams(NamedList* params) {
	    TelEngine::destruct(m_parsedParams);
	    m_parsedParams = params;
	}

    /**
     * Parse a received SDP body, process session level parameters.
     * Replace 
     * @param sdp Pointer to received SDP body
     */
    inline void processSessionParams(const MimeSdpBody* sdp)
	{ setSessionParams(parseSessionParams(sdp)); }

    /**
     * Update SDP session level parameters to be used when building SDP
     * @param params Parameters list
     */
    void updateSessionParams(const NamedList& params);

    /**
     * Update RTP forward related parameters
     * @param params Parameters list
     * @param inAccept Incoming session accepted
     */
    void updateRtpForward(const NamedList& params, bool inAccept = false);

    /**
     * Retrieve SDP forward flags
     * @param mask Mask to apply
     * @return Masked SDP forward flags
     */
    inline unsigned int sdpForward(unsigned int mask) const
	{ return m_sdpForward & mask; }

    SDPParser* m_parser;
    int m_mediaStatus;
    bool m_rtpForward;                   // Forward RTP flag
    AtomicUInt m_sdpForward;             // Forward SDP (only if RTP is forwarded)
    String m_originAddr;                 // Our SDP origin address
    String m_externalAddr;               // Our external IP address, possibly outside of a NAT
    String m_rtpAddr;                    // Remote RTP address
    String m_rtpLocalAddr;               // Local RTP address
    String m_rtpNatAddr;                 // Advertised local IP in sdp (override any local IP)
    ObjList* m_rtpMedia;                 // List of media descriptors
    int m_sdpSession;                    // Unique SDP session number
    int m_sdpVersion;                    // SDP version number, incremented each time we generate a new SDP
    unsigned int m_sdpHash;              // SDP content hash
    String m_host;
    bool m_secure;
    Rfc2833 m_rfc2833;                   // Payloads of RFC 2833 for remote party
    bool m_ipv6;                         // IPv6 support
    bool m_gpmd;                         // Handle GPMD even if not RTP forwarding
    NamedList m_amrExtra;                // Extra AMR codec parameters
    NamedList* m_parsedParams;           // Parsed session level parameters
    NamedList m_createSdpParams;         // Session level parameters used on SDP create
    String m_lastSdpFwd;                 // Last forwarded SDP

private:
    // Add extra AMR params to fmtp line
    void addFmtpAmrExtra(String& buf, const String* fmtp);
    // Add session or media parameters when creating SDP
    void addSdpParams(MimeSdpBody* sdp, const NamedList& params, bool* enc = 0, bool* dir = 0);

    DebugEnabler* m_enabler;             // Debug enabler used for output
    void* m_ptr;                         // Pointer to show in debug messages
    String m_traceId;
};

/**
 * This class holds a SDP parser and additional data used by SDP objects
 * @short A SDP parser
 */
class YSDP_API SDPParser : public DebugEnabler, public Mutex
{
    friend class SDPSession;

public:
    /**
     * SDP forward bahaviour flags
     */
    enum SdpForwardFlags {
	SdpForward = 0x01,               // SDP forward is enabled
	SdpFwdKeepLast = 0x02,           // Keep last forwarded SDP, possibly send in future offers
	SdpFwdProvSendLast = 0x10,       // Send last kept SDP in provisional message if missing signalling message
	SdpFwdProvPresentOnly = 0x20,    // Send SDP in provisional message only if present signalling message
	SdpFwdAnswerSendLast = 0x40,     // Send last kept SDP in answer message if missing signalling message
	SdpFwdAnswerPresentOnly = 0x80,  // Send SDP in answer message only if present signalling message
	// Masks
	SdpFwdProv = SdpFwdProvSendLast | SdpFwdProvPresentOnly,
	SdpFwdAnswer = SdpFwdAnswerSendLast | SdpFwdAnswerPresentOnly,
	SdpFwdAll = SdpFwdKeepLast | SdpFwdProv | SdpFwdAnswer,
    };

    /**
     * Constructor
     * @param dbgName Debug name of this parser
     * @param sessName Name of the session in SDP
     * @param fmts Default media formats
     */
    inline SDPParser(const char* dbgName, const char* sessName, const char* fmts = "alaw,mulaw")
	: Mutex(true,"SDPParser"),
	  m_sdpForward(0), m_secure(false), m_gpmd(false), m_ignorePort(false),
	  m_sessionName(sessName), m_audioFormats(fmts),
	  m_codecs(""), m_hacks("")
	{ debugName(dbgName); }

    /**
     * Get the formats list
     * This method is thread safe
     * @param buf String to be filled with comma separated list of formats
     */
    inline void getAudioFormats(String& buf)
	{ Lock lock(this); buf = m_audioFormats; }

    /**
     * Get the RFC 2833 offer payloads
     * @return Payloads for RFC 2883 telephony events
     */
    inline Rfc2833 rfc2833() const
	{ return m_rfc2833; }

    /**
     * Get the secure offer flag
     * @return True if SDES descriptors for SRTP will be offered
     */
    inline bool secure() const
	{ return m_secure; }

    /**
     * Get the propagate GPMD flag
     * @return True if session GPMD will be set in SDP when not forwarding
     */
    inline bool gpmd() const
	{ return m_gpmd; }

    /**
     * Get the SDP forward flag
     * @return Raw SDP forward flags
     */
    inline unsigned int sdpForward() const
	{ return m_sdpForward; }

    /**
     * Get the RTP port change ignore flag
     * @return True if a port change should not cause an offer change
     */
    inline bool ignorePort() const
	{ return m_ignorePort; }

    /**
     * Parse a received SDP body
     * This method is thread safe
     * @param sdp Received SDP body
     * @param addr Remote address
     * @param oldMedia Optional list of existing media (an already existing media
     *  will be moved to returned list)
     * @param media Optional expected media type. If not empty this will be the
     *  only media type returned (if found)
     * @param force Force updating formats even if incompatible with old ones
     * @param handleDir Handle media direction, false to ignore it
     * @return List of SDPMedia objects, may be NULL
     */
    ObjList* parse(const MimeSdpBody& sdp, String& addr, ObjList* oldMedia = 0,
	const String& media = String::empty(), bool force = false, bool handleDir = true);

    /**
     * Parse a received SDP body, returns NULL if SDP is not present
     * This method is thread safe
     * @param sdp Pointer to received SDP body
     * @param addr Remote address
     * @param oldMedia Optional list of existing media (an already existing media
     *  will be moved to returned list)
     * @param media Optional expected media type. If not empty this will be the
     *  only media type returned (if found)
     * @param force Force updating formats even if incompatible with old ones
     * @param handleDir Handle media direction, false to ignore it
     * @return List of SDPMedia objects, may be NULL
     */
    inline ObjList* parse(const MimeSdpBody* sdp, String& addr, ObjList* oldMedia = 0,
	const String& media = String::empty(), bool force = false, bool handleDir = true)
	{ return sdp ? parse(*sdp,addr,oldMedia,media,force,handleDir) : 0; }

    /**
     * Update configuration. This method should be called after a configuration file is loaded
     * @param codecs List of supported codecs
     * @param hacks List of hacks
     * @param general List of general settings
     */
    void initialize(const NamedList* codecs, const NamedList* hacks, const NamedList* general = 0);

    /**
     * Retrieve SDP forward from string
     * @param str String with value
     * @param defVal Default value if empty
     * @return SDP forward flags
     */
    static inline unsigned int getSdpForward(const String& str, unsigned int defVal = 0) {
	    if (str) {
		if (str.isBoolean())
		    return str.toBoolean() ? SdpForward : 0;
		defVal = str.encodeFlags(s_sdpForwardFlags);
		if (defVal)
		    defVal |= SdpForward;
	    }
	    return defVal;
	}

    /**
     * Yate Payloads for the AV profile
     */
    static const TokenDict s_payloads[];

    /**
     * SDP Payloads for the AV profile
     */
    static const TokenDict s_rtpmap[];

    /**
     * SDP forward flags
     */
    static const TokenDict s_sdpForwardFlags[];

private:
    Rfc2833 m_rfc2833;                   // RFC 2833 payloads offered to remote
    AtomicUInt m_sdpForward;             // Include raw SDP for forwarding
    bool m_secure;                       // Offer SRTP
    bool m_gpmd;                         // Propagate GPMD when not forwarding
    bool m_ignorePort;                   // Ignore port only changes in SDP
    String m_sessionName;
    String m_audioFormats;               // Default audio formats to be advertised to remote party
    NamedList m_codecs;                  // Codecs configuration list
    NamedList m_hacks;                   // Various potentially standard breaking settings
    String m_ssdpParam;                  // Session level parameters prefix
};

}; // namespace TelEngine

#endif /* __YATESDP_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
