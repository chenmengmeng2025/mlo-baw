#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/config-store-module.h"
#include "ns3/log.h"
using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Wifi6SimpleExample");

// 自定义回调函数：STA 发送数据包时调用
void NotifyAppTx(Ptr<const Packet> packet)
{
    Time now = Simulator::Now();
    std::cout << "STA Tx at " << now.GetSeconds() << "s, UID " 
              << packet->GetUid() << std::endl;
}

// 自定义回调函数：AP 接收数据包时调用
void NotifyAppRx(Ptr<const Packet> packet)
{
    Time now = Simulator::Now();
    std::cout << "AP Rx at " << now.GetSeconds() << "s, UID " 
              << packet->GetUid() << std::endl;
}

int main(int argc, char *argv[])
{  LogComponentEnable("TcpSocketBase", LOG_LEVEL_DEBUG);
    // 启用日志组件
    LogComponentEnable("Wifi6SimpleExample", LOG_LEVEL_INFO);

    // 创建节点
    NodeContainer apNode;
    NodeContainer staNode;
    apNode.Create(1);
    staNode.Create(1);

    // 创建无线设备
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);  // 使用 Wi-Fi 6 (802.11ax) 标准

    SpectrumWifiPhyHelper phy(1); 
    phy.SetPcapDataLinkType(YansWifiPhyHelper::DLT_IEEE802_11_RADIO);

    // 设置频段为 2.4 GHz
    double txPower = 16; 
    Ptr<MultiModelSpectrumChannel> spectrumChannel_2 = CreateObject<MultiModelSpectrumChannel>();
    Ptr<LogDistancePropagationLossModel> lossModel_2 = CreateObject<LogDistancePropagationLossModel>();
    lossModel_2->SetAttribute("Exponent", DoubleValue(2.0));
    lossModel_2->SetAttribute("ReferenceDistance", DoubleValue(1.0));
    lossModel_2->SetAttribute("ReferenceLoss", DoubleValue(40.046));
    spectrumChannel_2->AddPropagationLossModel(lossModel_2);
    phy.AddChannel(spectrumChannel_2, WIFI_SPECTRUM_2_4_GHZ);
    phy.Set("ChannelSettings", StringValue("{0, 20, BAND_2_4GHZ, 0}"));
    phy.Set("TxPowerStart", DoubleValue(txPower));
    phy.Set("TxPowerEnd", DoubleValue(txPower));
    
    // 配置物理层参数
    // phy.SetErrorRateModel("ns3::NistErrorRateModel");
    phy.SetErrorRateModel("ns3::TableBasedErrorRateModel");


    // 配置 MAC 层参数
    WifiMacHelper mac;
    Ssid ssid = Ssid("wifi-6-ssid");
    mac.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssid),
                "ActiveProbing", BooleanValue(false));

    NetDeviceContainer staDevices = wifi.Install(phy, mac, staNode);

    mac.SetType("ns3::ApWifiMac",
                "Ssid", SsidValue(ssid));

    NetDeviceContainer apDevices = wifi.Install(phy, mac, apNode);

    // 配置移动模型
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0.0, 0.0, 0.0));  // AP 位置
    positionAlloc->Add(Vector(5.0, 0.0, 0.0));  // STA 位置
    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(apNode);
    mobility.Install(staNode);

    // 配置互联网协议栈
    InternetStackHelper stack;
    stack.Install(apNode);
    stack.Install(staNode);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer apInterfaces = address.Assign(apDevices);
    Ipv4InterfaceContainer staInterfaces = address.Assign(staDevices);

    // 配置应用程序
    uint16_t port = 9;  
    PacketSinkHelper sink("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApps = sink.Install(staNode);
    sinkApps.Start(Seconds(0.0));
    sinkApps.Stop(Seconds(10.0));

    // BulkSendHelper source("ns3::TcpSocketFactory", InetSocketAddress(staInterfaces.GetAddress(0), port));
    // // Set the amount of data to send in bytes.  Zero is unlimited.
    // source.SetAttribute("MaxBytes", UintegerValue(0));
    // ApplicationContainer sourceApps = source.Install(apNode);
    // sourceApps.Start(Seconds(0.0));
    // sourceApps.Stop(Seconds(2.0));
    OnOffHelper onoff("ns3::TcpSocketFactory", Ipv4Address::GetAny());
    // OnOffHelper onoff("ns3::TcpSocketFactory", InetSocketAddress(staInterfaces.GetAddress(0), port));
    AddressValue remoteAddress(InetSocketAddress(staInterfaces.GetAddress(0), port));
    onoff.SetAttribute("Remote", remoteAddress);
    onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    onoff.SetAttribute("PacketSize", UintegerValue(1448));
    onoff.SetAttribute("DataRate", StringValue("1000Mbps"));
    ApplicationContainer sourceApps = onoff.Install(apNode);
    sourceApps.Start(Seconds(0.0));
    sourceApps.Stop(Seconds(2.0));
    uint32_t maxAmpduSize{1024 * 4 * (1448 + 150)}; // 1048575
    uint32_t mpduBufferSize{256};
    Config::Set("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_MaxAmpduSize",
                UintegerValue(maxAmpduSize));

    Config::Set("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MpduBufferSize",
                UintegerValue(mpduBufferSize));
    Config::Set("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Aifsns", AttributeContainerValue<UintegerValue>(std::list<uint64_t>{2}));
    Config::Set("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MinCws", AttributeContainerValue<UintegerValue>(std::list<int>{1}));
    Config::Set("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/MaxCws", AttributeContainerValue<UintegerValue>(std::list<int>{3}));
    phy.EnablePcap("ap-ax-trace", apDevices.Get(0));
    phy.EnablePcap("sta-ax-trace", staDevices.Get(0));

    // 启动仿真
    Simulator::Stop(Seconds(2.0));
    Simulator::Run();
    Simulator::Destroy();
    return 0;
}