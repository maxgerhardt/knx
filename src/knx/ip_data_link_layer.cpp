#include "config.h"
#ifdef USE_IP

#include "ip_data_link_layer.h"

#include "bits.h"
#include "platform.h"
#include "device_object.h"
#include "knx_ip_routing_indication.h"
#include "knx_ip_search_request.h"
#include "knx_ip_search_response.h"
#include "knx_facade.h"
#ifdef KNX_TUNNELING
#include "knx_ip_connect_request.h"
#include "knx_ip_connect_response.h"
#include "knx_ip_state_request.h"
#include "knx_ip_state_response.h"
#include "knx_ip_disconnect_request.h"
#include "knx_ip_disconnect_response.h"
#include "knx_ip_tunneling_request.h"
#include "knx_ip_tunneling_ack.h"
#include "knx_ip_description_request.h"
#include "knx_ip_description_response.h"
#include "knx_ip_config_request.h"
#endif

#include <stdio.h>
#include <string.h>

#define KNXIP_HEADER_LEN 0x6
#define KNXIP_PROTOCOL_VERSION 0x10

#define MIN_LEN_CEMI 10

IpDataLinkLayer::IpDataLinkLayer(DeviceObject& devObj, IpParameterObject& ipParam,
    NetworkLayerEntity &netLayerEntity, Platform& platform) : DataLinkLayer(devObj, netLayerEntity, platform), _ipParameters(ipParam)
{
}

bool IpDataLinkLayer::sendFrame(CemiFrame& frame)
{
    KnxIpRoutingIndication packet(frame);
    // only send 50 packet per second: see KNX 3.2.6 p.6
    if(isSendLimitReached())
        return false;
    bool success = sendBytes(packet.data(), packet.totalLength());
#ifdef KNX_ACTIVITYCALLBACK
        knx.Activity((_netIndex << KNX_ACTIVITYCALLBACK_NET) | (KNX_ACTIVITYCALLBACK_DIR_SEND << KNX_ACTIVITYCALLBACK_DIR));
#endif
    dataConReceived(frame, success);
    return success;
}

#ifdef KNX_TUNNELING
void IpDataLinkLayer::dataRequestToTunnel(CemiFrame& frame)
{
    if(frame.addressType() == AddressType::GroupAddress)
    {
        for(int i = 0; i < KNX_TUNNELING; i++)
            if(tunnels[i].ChannelId != 0 && tunnels[i].IndividualAddress == frame.sourceAddress())
                sendFrameToTunnel(&tunnels[i], frame);
                //TODO check if source is from tunnel
        return;
    }

    KnxIpTunnelConnection *tun = nullptr;
    for(int i = 0; i < KNX_TUNNELING; i++)
    {
        if(tunnels[i].IndividualAddress == frame.sourceAddress())
            continue;

        if(tunnels[i].IndividualAddress == frame.destinationAddress())
        {
            tun = &tunnels[i];
            break;
        }
    }

    if(tun == nullptr)
    {
        for(int i = 0; i < KNX_TUNNELING; i++)
        {
            if(tunnels[i].IsConfig)
            {
                println("Found config Channel");
                tun = &tunnels[i];
                break;
            }
        }
    }

    if(tun == nullptr)
    {
#ifdef KNX_LOG_TUNNELING
        print("Found no Tunnel for IA: ");
        println(frame.destinationAddress(), 16);
#endif
        return;
    }

    sendFrameToTunnel(tun, frame);
}

void IpDataLinkLayer::dataConfirmationToTunnel(CemiFrame& frame)
{
    if(frame.addressType() == AddressType::GroupAddress)
    {
        for(int i = 0; i < KNX_TUNNELING; i++)
            if(tunnels[i].ChannelId != 0 && tunnels[i].IndividualAddress == frame.sourceAddress())
                sendFrameToTunnel(&tunnels[i], frame);
                //TODO check if source is from tunnel
        return;
    }

    KnxIpTunnelConnection *tun = nullptr;
    for(int i = 0; i < KNX_TUNNELING; i++)
    {
        if(tunnels[i].IndividualAddress == frame.destinationAddress())
            continue;
            
        if(tunnels[i].IndividualAddress == frame.sourceAddress())
        {
            tun = &tunnels[i];
            break;
        }
    }

    if(tun == nullptr)
    {
        for(int i = 0; i < KNX_TUNNELING; i++)
        {
            if(tunnels[i].IsConfig)
            {
                println("Found config Channel");
                tun = &tunnels[i];
                break;
            }
        }
    }

    if(tun == nullptr)
    {
#ifdef KNX_LOG_TUNNELING
        print("Found no Tunnel for IA: ");
        println(frame.destinationAddress(), 16);
#endif
        return;
    }

    sendFrameToTunnel(tun, frame);
}

void IpDataLinkLayer::dataIndicationToTunnel(CemiFrame& frame)
{
    if(frame.addressType() == AddressType::GroupAddress)
    {
        for(int i = 0; i < KNX_TUNNELING; i++)
            if(tunnels[i].ChannelId != 0 && tunnels[i].IndividualAddress != frame.sourceAddress())
                sendFrameToTunnel(&tunnels[i], frame);
        return;
    }

    KnxIpTunnelConnection *tun = nullptr;
    for(int i = 0; i < KNX_TUNNELING; i++)
    {
        if(tunnels[i].IndividualAddress == frame.sourceAddress())
            continue;
            
        if(tunnels[i].IndividualAddress == frame.destinationAddress())
        {
            tun = &tunnels[i];
            break;
        }
    }

    if(tun == nullptr)
    {
        for(int i = 0; i < KNX_TUNNELING; i++)
        {
            if(tunnels[i].IsConfig)
            {
                println("Found config Channel");
                tun = &tunnels[i];
                break;
            }
        }
    }

    if(tun == nullptr)
    {
#ifdef KNX_LOG_TUNNELING
        print("Found no Tunnel for IA: ");
        println(frame.destinationAddress(), 16);
#endif
        return;
    }

    sendFrameToTunnel(tun, frame);
}

void IpDataLinkLayer::sendFrameToTunnel(KnxIpTunnelConnection *tunnel, CemiFrame& frame)
{
#ifdef KNX_LOG_TUNNELING
    print("Send to Channel: ");
    println(tunnel->ChannelId, 16);
#endif
    KnxIpTunnelingRequest req(frame);
    req.connectionHeader().sequenceCounter(tunnel->SequenceCounter_S++);
    req.connectionHeader().length(LEN_CH);
    req.connectionHeader().channelId(tunnel->ChannelId);

    if(frame.messageCode() != L_data_req && frame.messageCode() != L_data_con && frame.messageCode() != L_data_ind)
        req.serviceTypeIdentifier(DeviceConfigurationRequest);

    _platform.sendBytesUniCast(tunnel->IpAddress, tunnel->PortData, req.data(), req.totalLength());
}
#endif

void IpDataLinkLayer::loop()
{
    if (!_enabled)
        return;

#ifdef KNX_TUNNELING
    for(int i = 0; i < KNX_TUNNELING; i++)
    {
        if(tunnels[i].ChannelId != 0)
        {
            if(millis() - tunnels[i].lastHeartbeat > 120000)
            {
    #ifdef KNX_LOG_TUNNELING
                print("Closed Tunnel 0x");
                print(tunnels[i].ChannelId, 16);
                println(" due to no heartbeat in 30 seconds");
    #endif
                KnxIpDisconnectRequest discReq;
                discReq.channelId(tunnels[i].ChannelId);
                discReq.hpaiCtrl().length(LEN_IPHPAI);
                discReq.hpaiCtrl().code(IPV4_UDP);
                discReq.hpaiCtrl().ipAddress(tunnels[i].IpAddress);
                discReq.hpaiCtrl().ipPortNumber(tunnels[i].PortCtrl);
                _platform.sendBytesUniCast(tunnels[i].IpAddress, tunnels[i].PortCtrl, discReq.data(), discReq.totalLength());
                tunnels[i].Reset();
            }
            break;
        }
    }
#endif


    uint8_t buffer[512];
    uint32_t src_ip = 0;
    uint16_t src_port = 0;
    int len = _platform.readBytesMultiCast(buffer, 512, src_ip, src_port);
    if (len <= 0)
        return;

    if (len < KNXIP_HEADER_LEN)
        return;
    
    if (buffer[0] != KNXIP_HEADER_LEN 
        || buffer[1] != KNXIP_PROTOCOL_VERSION)
        return;

#ifdef KNX_ACTIVITYCALLBACK
    knx.Activity((_netIndex << KNX_ACTIVITYCALLBACK_NET) | (KNX_ACTIVITYCALLBACK_DIR_RECV << KNX_ACTIVITYCALLBACK_DIR));
#endif

    uint16_t code;
    popWord(code, buffer + 2);
    switch ((KnxIpServiceType)code)
    {
        case RoutingIndication:
        {
            KnxIpRoutingIndication routingIndication(buffer, len);
            frameReceived(routingIndication.frame());
            break;
        }
        
        case SearchRequest:
        {
            KnxIpSearchRequest searchRequest(buffer, len);
            KnxIpSearchResponse searchResponse(_ipParameters, _deviceObject);

            auto hpai = searchRequest.hpai();
#ifdef KNX_ACTIVITYCALLBACK
            knx.Activity((_netIndex << KNX_ACTIVITYCALLBACK_NET) | (KNX_ACTIVITYCALLBACK_DIR_SEND << KNX_ACTIVITYCALLBACK_DIR) | (KNX_ACTIVITYCALLBACK_IPUNICAST));
#endif
            _platform.sendBytesUniCast(hpai.ipAddress(), hpai.ipPortNumber(), searchResponse.data(), searchResponse.totalLength());
            break;
        }
        
        case SearchRequestExt:
        {
            // FIXME, implement (not needed atm)
            break;
        }

#ifdef KNX_TUNNELING
        case ConnectRequest:
        {
            loopHandleConnectRequest(buffer, len);
            break;
        }

        case ConnectionStateRequest:
        {
            loopHandleConnectionStateRequest(buffer, len);
            break;
        }

        case DisconnectRequest:
        {
            loopHandleDisconnectRequest(buffer, len);
            break;;
        }

        case DescriptionRequest:
        {
            loopHandleDescriptionRequest(buffer, len, src_ip, src_port);
            break;
        }

        case DeviceConfigurationRequest:
        {
            loopHandleDeviceConfigurationRequest(buffer, len);
            break;
        }

        case TunnelingRequest:
        {
            loopHandleTunnelingRequest(buffer, len);
            return;
        }

        case DeviceConfigurationAck:
        {
            //TOOD nothing to do now
            //println("got Ack");
            break;
        }

        case TunnelingAck:
        {
            //TOOD nothing to do now
            //println("got Ack");
            break;
        }
#endif
        default:
#if defined(KNX_LOG_TUNNELING) || defined(KNX_LOG_IP)
            print("Unhandled service identifier: ");
            println(code, HEX);
#endif
            break;
    }
}

#ifdef KNX_TUNNELING
void IpDataLinkLayer::loopHandleConnectRequest(uint8_t* buffer, uint16_t length)
{
    KnxIpConnectRequest connRequest(buffer, length);
#ifdef KNX_LOG_TUNNELING
    println("Got Connect Request!");
    switch(connRequest.cri().type())
    {
        case DEVICE_MGMT_CONNECTION:
            println("Device Management Connection");
            break;
        case TUNNEL_CONNECTION:
            println("Tunnel Connection");
            break;
        case REMLOG_CONNECTION:
            println("RemLog Connection");
            break;
        case REMCONF_CONNECTION:
            println("RemConf Connection");
            break;
        case OBJSVR_CONNECTION:
            println("ObjectServer Connection");
            break;
    }
    
    print("Data Endpoint: ");
    uint32_t ip = connRequest.hpaiData().ipAddress();
    print(ip >> 24);
    print(".");
    print((ip >> 16) & 0xFF);
    print(".");
    print((ip >> 8) & 0xFF);
    print(".");
    print(ip & 0xFF);
    print(":");
    println(connRequest.hpaiData().ipPortNumber());
    print("Ctrl Endpoint: ");
    ip = connRequest.hpaiCtrl().ipAddress();
    print(ip >> 24);
    print(".");
    print((ip >> 16) & 0xFF);
    print(".");
    print((ip >> 8) & 0xFF);
    print(".");
    print(ip & 0xFF);
    print(":");
    println(connRequest.hpaiCtrl().ipPortNumber());
#endif

    //We only support 0x04!
    if(connRequest.cri().type() != TUNNEL_CONNECTION && connRequest.cri().type() != DEVICE_MGMT_CONNECTION)
    {
#ifdef KNX_LOG_TUNNELING
        println("Only Tunnel/DeviceMgmt Connection ist supported!");
#endif
        KnxIpConnectResponse connRes(0x00, E_CONNECTION_TYPE);
        _platform.sendBytesUniCast(connRequest.hpaiCtrl().ipAddress(), connRequest.hpaiCtrl().ipPortNumber(), connRes.data(), connRes.totalLength());
        return;
    }

    if(connRequest.cri().type() == TUNNEL_CONNECTION && connRequest.cri().layer() != 0x02) //LinkLayer
    {
        //We only support 0x02!
#ifdef KNX_LOG_TUNNELING
    println("Only LinkLayer ist supported!");
#endif
        KnxIpConnectResponse connRes(0x00, E_TUNNELING_LAYER);
        _platform.sendBytesUniCast(connRequest.hpaiCtrl().ipAddress(), connRequest.hpaiCtrl().ipPortNumber(), connRes.data(), connRes.totalLength());
        return;
    }

    KnxIpTunnelConnection *tun = nullptr;
    for(int i = 0; i < KNX_TUNNELING; i++)
    {
        if(tunnels[i].ChannelId == 0)
        {
            tun = &tunnels[i];
            break;
        }
    }

    bool hasDoublePA = false;
    const uint8_t *addresses = _ipParameters.propertyData(PID_ADDITIONAL_INDIVIDUAL_ADDRESSES);
    for(int i = 0; i < KNX_TUNNELING; i++)
    {
        uint16_t pa = 0;
        popWord(pa, addresses + (i*2));

        for(int x = 0; x < KNX_TUNNELING; x++)
        {
            uint16_t pa2 = 0;
            popWord(pa2, addresses + (x*2));
            if(i != x && pa == pa2)
            {
                hasDoublePA = true;
                break;
            }
        }

        bool isInUse = false;
        for(int x = 0; x < KNX_TUNNELING; x++)
        {
            if(tunnels[x].IndividualAddress == pa)
                isInUse = true;
        }

        if(isInUse)
            continue;

        if(hasDoublePA)
            println("Not Unique ");
        tun->IndividualAddress = pa;
        break;
    }

    if(tun == nullptr)
    {
        println("Kein freier Tunnel verfuegbar");
        KnxIpConnectResponse connRes(0x00, E_NO_MORE_CONNECTIONS);
        _platform.sendBytesUniCast(connRequest.hpaiCtrl().ipAddress(), connRequest.hpaiCtrl().ipPortNumber(), connRes.data(), connRes.totalLength());
        return;
    }

    if(connRequest.cri().type() == DEVICE_MGMT_CONNECTION)
        tun->IsConfig = true;
    tun->ChannelId = _lastChannelId++;
    tun->lastHeartbeat = millis();
    if(_lastChannelId == 0)
        _lastChannelId++;

#ifdef KNX_LOG_TUNNELING
    print("Neuer Tunnel: 0x");
    print(tun->ChannelId, 16);
    print("/");
    print(tun->IndividualAddress >> 12);
    print(".");
    print((tun->IndividualAddress >> 8) & 0xF);
    print(".");
    println(tun->IndividualAddress & 0xFF);
#endif

    tun->IpAddress = connRequest.hpaiData().ipAddress();
    tun->PortData = connRequest.hpaiData().ipPortNumber();
    tun->PortCtrl = connRequest.hpaiCtrl().ipPortNumber();

    KnxIpConnectResponse connRes(_ipParameters, tun->IndividualAddress, 3671, tun->ChannelId, connRequest.cri().type());
    _platform.sendBytesUniCast(tun->IpAddress, tun->PortCtrl, connRes.data(), connRes.totalLength());
}

void IpDataLinkLayer::loopHandleConnectionStateRequest(uint8_t* buffer, uint16_t length)
{
    KnxIpStateRequest stateRequest(buffer, length);

    KnxIpTunnelConnection *tun = nullptr;
    for(int i = 0; i < KNX_TUNNELING; i++)
    {
        if(tunnels[i].ChannelId == stateRequest.channelId())
        {
            tun = &tunnels[i];
            break;
        }
    }

    if(tun == nullptr)
    {
#ifdef KNX_LOG_TUNNELING
        print("Channel ID nicht gefunden: ");
        println(stateRequest.channelId());
#endif
        KnxIpStateResponse stateRes(0x00, E_CONNECTION_ID);
        _platform.sendBytesUniCast(stateRequest.hpaiCtrl().ipAddress(), stateRequest.hpaiCtrl().ipPortNumber(), stateRes.data(), stateRes.totalLength());
        return;
    }

    //TODO check knx connection!
    //if no connection return E_KNX_CONNECTION

    //TODO check when to send E_DATA_CONNECTION

    tun->lastHeartbeat = millis();
    KnxIpStateResponse stateRes(tun->ChannelId, E_NO_ERROR);
    _platform.sendBytesUniCast(stateRequest.hpaiCtrl().ipAddress(), stateRequest.hpaiCtrl().ipPortNumber(), stateRes.data(), stateRes.totalLength());
}

void IpDataLinkLayer::loopHandleDisconnectRequest(uint8_t* buffer, uint16_t length)
{
    KnxIpDisconnectRequest discReq(buffer, length);
            
#ifdef KNX_LOG_TUNNELING
    print("Disconnect Channel ID: ");
    println(discReq.channelId());
#endif
    
    KnxIpTunnelConnection *tun = nullptr;
    for(int i = 0; i < KNX_TUNNELING; i++)
    {
        if(tunnels[i].ChannelId == discReq.channelId())
        {
            tun = &tunnels[i];
            break;
        }
    }

    if(tun == nullptr)
    {
#ifdef KNX_LOG_TUNNELING
        print("Channel ID nicht gefunden: ");
        println(discReq.channelId());
#endif
        KnxIpDisconnectResponse discRes(0x00, E_CONNECTION_ID);
        _platform.sendBytesUniCast(discReq.hpaiCtrl().ipAddress(), discReq.hpaiCtrl().ipPortNumber(), discRes.data(), discRes.totalLength());
        return;
    }


    KnxIpDisconnectResponse discRes(tun->ChannelId, E_NO_ERROR);
    _platform.sendBytesUniCast(discReq.hpaiCtrl().ipAddress(), discReq.hpaiCtrl().ipPortNumber(), discRes.data(), discRes.totalLength());
    tun->Reset();
}

void IpDataLinkLayer::loopHandleDescriptionRequest(uint8_t* buffer, uint16_t length, uint32_t src_ip, uint16_t src_port)
{
    KnxIpDescriptionRequest descReq(buffer, length);
    KnxIpDescriptionResponse descRes(_ipParameters, _deviceObject);
    uint32_t remote_ip = descReq.hpaiCtrl().ipAddress();
    uint16_t remote_port = descReq.hpaiCtrl().ipPortNumber();
    if(descReq.hpaiCtrl().ipAddress() == 0 || descReq.hpaiCtrl().ipPortNumber() == 0)
    {
        remote_ip = src_ip;
        remote_port = src_port;
        if(src_ip == 0 || src_port == 0)
        {
            println("drop KnxIpDescriptionRequest, unknown sender");
            return;
        }
    }

    _platform.sendBytesUniCast(remote_ip, remote_port, descRes.data(), descRes.totalLength());
}

void IpDataLinkLayer::loopHandleDeviceConfigurationRequest(uint8_t* buffer, uint16_t length)
{
    KnxIpConfigRequest confReq(buffer, length);
    
    KnxIpTunnelConnection *tun = nullptr;
    for(int i = 0; i < KNX_TUNNELING; i++)
    {
        if(tunnels[i].ChannelId == confReq.connectionHeader().channelId())
        {
            tun = &tunnels[i];
            break;
        }
    }

    if(tun == nullptr)
    {
        print("Channel ID nicht gefunden: ");
        println(confReq.connectionHeader().channelId());
        KnxIpStateResponse stateRes(0x00, E_CONNECTION_ID);
        //TODO where to send?
        //_platform.sendBytesUniCast(tun->IpAddress, tun->PortCtrl, stateRes.data(), stateRes.totalLength());
        return;
    }

    KnxIpTunnelingAck tunnAck;
    tunnAck.serviceTypeIdentifier(DeviceConfigurationAck);
    tunnAck.connectionHeader().length(4);
    tunnAck.connectionHeader().channelId(tun->ChannelId);
    tunnAck.connectionHeader().sequenceCounter(confReq.connectionHeader().sequenceCounter());
    tunnAck.connectionHeader().status(E_NO_ERROR);
    _platform.sendBytesUniCast(tun->IpAddress, tun->PortData, tunnAck.data(), tunnAck.totalLength());

    tun->lastHeartbeat = millis();
    _cemiServer->frameReceived(confReq.frame());
}

void IpDataLinkLayer::loopHandleTunnelingRequest(uint8_t* buffer, uint16_t length)
{
    KnxIpTunnelingRequest tunnReq(buffer, length);

    KnxIpTunnelConnection *tun = nullptr;
    for(int i = 0; i < KNX_TUNNELING; i++)
    {
        if(tunnels[i].ChannelId == tunnReq.connectionHeader().channelId())
        {
            tun = &tunnels[i];
            break;
        }
    }

    if(tun == nullptr)
    {
#ifdef KNX_LOG_TUNNELING
        print("Channel ID nicht gefunden: ");
        println(tunnReq.connectionHeader().channelId());
#endif
        KnxIpStateResponse stateRes(0x00, E_CONNECTION_ID);
        //TODO where to send?
        //_platform.sendBytesUniCast(stateRequest.hpaiCtrl().ipAddress(), stateRequest.hpaiCtrl().ipPortNumber(), stateRes.data(), stateRes.totalLength());
        return;
    }

    uint8_t sequence = tunnReq.connectionHeader().sequenceCounter();
    if(sequence == tun->SequenceCounter_R)
    {
#ifdef KNX_LOG_TUNNELING
        print("Received SequenceCounter again: ");
        println(tunnReq.connectionHeader().sequenceCounter());
#endif
        //we already got this one
        //so just ack it
        KnxIpTunnelingAck tunnAck;
        tunnAck.connectionHeader().length(4);
        tunnAck.connectionHeader().channelId(tun->ChannelId);
        tunnAck.connectionHeader().sequenceCounter(tunnReq.connectionHeader().sequenceCounter());
        tunnAck.connectionHeader().status(E_NO_ERROR);
        _platform.sendBytesUniCast(tun->IpAddress, tun->PortData, tunnAck.data(), tunnAck.totalLength());
        return;
    } else if((uint8_t)(sequence - 1) != tun->SequenceCounter_R) {
#ifdef KNX_LOG_TUNNELING
        print("Wrong SequenceCounter: got ");
        print(tunnReq.connectionHeader().sequenceCounter());
        print(" expected ");
        println((uint8_t)(tun->SequenceCounter_R + 1));
#endif
        //Dont handle it
        return;
    }
    
    KnxIpTunnelingAck tunnAck;
    tunnAck.connectionHeader().length(4);
    tunnAck.connectionHeader().channelId(tun->ChannelId);
    tunnAck.connectionHeader().sequenceCounter(tunnReq.connectionHeader().sequenceCounter());
    tunnAck.connectionHeader().status(E_NO_ERROR);
    _platform.sendBytesUniCast(tun->IpAddress, tun->PortData, tunnAck.data(), tunnAck.totalLength());

    tun->SequenceCounter_R = tunnReq.connectionHeader().sequenceCounter();

    if(tunnReq.frame().sourceAddress() == 0)
        tunnReq.frame().sourceAddress(tun->IndividualAddress);

    _cemiServer->frameReceived(tunnReq.frame());
}
#endif

void IpDataLinkLayer::enabled(bool value)
{
//    _print("own address: ");
//    _println(_deviceObject.individualAddress());
    if (value && !_enabled)
    {
        _platform.setupMultiCast(_ipParameters.propertyValue<uint32_t>(PID_ROUTING_MULTICAST_ADDRESS), KNXIP_MULTICAST_PORT);
        _enabled = true;
        return;
    }

    if(!value && _enabled)
    {
        _platform.closeMultiCast();
        _enabled = false;
        return;
    }
}

bool IpDataLinkLayer::enabled() const
{
    return _enabled;
}

DptMedium IpDataLinkLayer::mediumType() const
{
    return DptMedium::KNX_IP;
}

bool IpDataLinkLayer::sendBytes(uint8_t* bytes, uint16_t length)
{
    if (!_enabled)
        return false;

    return _platform.sendBytesMultiCast(bytes, length);
}

bool IpDataLinkLayer::isSendLimitReached()
{
    uint32_t curTime = millis() / 100;

    // check if the countbuffer must be adjusted
    if(_frameCountTimeBase >= curTime)
    {
        uint32_t timeBaseDiff = _frameCountTimeBase - curTime;
        if(timeBaseDiff > 10)
            timeBaseDiff = 10;
        for(int i = 0; i < timeBaseDiff ; i++)
        {
            _frameCountBase++;
            _frameCountBase = _frameCountBase % 10;
            _frameCount[_frameCountBase] = 0;
        }
        _frameCountTimeBase = curTime;
    }
    else // _frameCountTimeBase < curTime => millis overflow, reset
    {
        for(int i = 0; i < 10 ; i++)
            _frameCount[i] = 0;
        _frameCountBase = 0;
        _frameCountTimeBase = curTime;
    }

    //check if we are over the limit
    uint16_t sum = 0;
    for(int i = 0; i < 10 ; i++)
        sum += _frameCount[i];
    if(sum > 50)
    {
        println("Dropping packet due to 50p/s limit");
        return true;   // drop packet
    }
    else
    {
        _frameCount[_frameCountBase]++;
        //print("sent packages in last 1000ms: ");
        //print(sum);
        //print(" curTime: ");
        //println(curTime);
        return false;
    }
}
#endif
