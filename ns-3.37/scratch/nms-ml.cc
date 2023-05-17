#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/gnuplot.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("NmsCC");

Time prevTime = Seconds(0);
Time measurementInterval = Seconds(0.2);
uint32_t *prevCubic, *prevBbr;
std::string expName = "num-opps-1";

std::string dataFileName = "training.csv";
std::ofstream dataFile(dataFileName, std::ios::out | std::ios::app);

Gnuplot tpPlot(expName + "-throughput.png");
Gnuplot rttPlot(expName + "-rtt.png");
Gnuplot2dDataset *cubicTpDataset, *bbrTpDataset;
Gnuplot2dDataset *cubicRttDataset, *bbrRttDataset;

Time stopTime = Seconds(100.0);
int senderCount = 9;
int bbrNodes = 1;

void
CheckQueueSize(Ptr<QueueDisc> qd)
{
    // uint32_t qsize = qd->GetCurrentSize().GetValue();

    Simulator::Schedule(measurementInterval, &CheckQueueSize, qd);
    // std::ofstream q(dir + "/queueSize.dat", std::ios::out | std::ios::app);
    // q << Simulator::Now().GetSeconds() << " " << qsize << std::endl;
    // q.close();
}

// Calculate throughput
static void
TraceThroughput(Ptr<FlowMonitor> monitor, Ptr<Ipv4FlowClassifier> classifier)
{
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();
    Time curTime = Now();
    double time = curTime.GetSeconds();

    
    int src[4], dst[4];

    for(auto itr = stats.begin(); itr != stats.end(); itr++) {
        
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(itr->first);
        // std::cout<<curTime.GetSeconds()<<": "<<itr->first<<std::endl;
        // std::cout<<"Reached here"<<std::endl;
        // std::cout<<t.sourceAddress<<" "<<t.destinationAddress<<std::endl;
        uint32_t srcAddr = t.sourceAddress.Get();
        uint32_t dstAddr = t.destinationAddress.Get();

        for(int i=3; i>=0; i--) {
            src[3-i] = int((srcAddr >> (8*i)) & 0xFF);
            dst[3-i] = int((dstAddr >> (8*i)) & 0xFF);
        }

        if(src[1] == 1 && dst[1] == 3) {
            double tp = 8 * (itr->second.txBytes - prevCubic[src[2]]) /
               (1000 * 1000 * (curTime.GetSeconds() - prevTime.GetSeconds()));
            Time rtt = itr->second.delaySum / itr->second.rxPackets;
            
            prevCubic[src[2]] = itr->second.txBytes;
            
            cubicTpDataset[src[2]].Add(time, tp);
            cubicRttDataset[src[2]].Add(time, rtt.GetMilliSeconds());

        } else if(src[1] == 2 && dst[1] == 4) {
            double tp = 8 * (itr->second.txBytes - prevBbr[src[2]]) /
               (1000 * 1000 * (curTime.GetSeconds() - prevTime.GetSeconds()));
            Time rtt = itr->second.delaySum / itr->second.rxPackets;
            
            prevBbr[src[2]] = itr->second.txBytes;
            // std::cout<<"Reached here"<<std::endl;

            bbrTpDataset[src[2]].Add(time, tp);
            bbrRttDataset[src[2]].Add(time, rtt.GetMilliSeconds());

            dataFile<<tp<<", "<<rtt.GetMicroSeconds()<<", "<<(senderCount - 1)<<std::endl;
        }
    }
    
    prevTime = curTime;
    
    Simulator::Schedule(measurementInterval, &TraceThroughput, monitor, classifier);
}

int main(int argc, char *argv[]) 
{
    LogComponentEnable("NmsCC", LOG_LEVEL_INFO);
    

    std::string queueDisc = std::string("ns3::FifoQueueDisc");
    // Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(4194304));
    // Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(6291456));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));
    Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(2));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448));
    Config::SetDefault(queueDisc + "::MaxSize", QueueSizeValue(QueueSize("10p")));

    // Number of nodes
    int receiverCount = senderCount, routerCount = 2;

    int cubicNodes = senderCount - bbrNodes;
    prevCubic = new uint32_t[cubicNodes];
    prevBbr = new uint32_t[bbrNodes];
    cubicTpDataset = new Gnuplot2dDataset[cubicNodes];
    bbrTpDataset = new Gnuplot2dDataset[bbrNodes];
    cubicRttDataset = new Gnuplot2dDataset[cubicNodes];
    bbrRttDataset = new Gnuplot2dDataset[bbrNodes];

    NodeContainer sender;
    NodeContainer receiver;
    NodeContainer routers;
    
    sender.Create(senderCount);
    receiver.Create(receiverCount);
    routers.Create(routerCount);


    // Define links
    PointToPointHelper bottleneckLink;
    bottleneckLink.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    bottleneckLink.SetChannelAttribute("Delay", StringValue("20ms"));

    PointToPointHelper edgeLink, cubicEdgeLink, bbrEdgeLink;
    edgeLink.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    edgeLink.SetChannelAttribute("Delay", StringValue("4ms"));

    cubicEdgeLink.SetDeviceAttribute("DataRate", StringValue("0.7Mbps"));
    cubicEdgeLink.SetChannelAttribute("Delay", StringValue("4ms"));

    bbrEdgeLink.SetDeviceAttribute("DataRate", StringValue("12Mbps"));
    bbrEdgeLink.SetChannelAttribute("Delay", StringValue("4ms"));

    // Install links
    NetDeviceContainer senderEdges[senderCount];
    NetDeviceContainer receiverEdges[receiverCount];
    NetDeviceContainer routerEdge;

    for(int i = 0; i < cubicNodes; i++) {
        senderEdges[i] = cubicEdgeLink.Install(sender.Get(i), routers.Get(0));
    }

    for(int i = cubicNodes; i < senderCount; i++) {
        senderEdges[i] = bbrEdgeLink.Install(sender.Get(i), routers.Get(0));
    }

    for(int i = 0; i < receiverCount; i++) {
        receiverEdges[i] = edgeLink.Install(routers.Get(1), receiver.Get(i));
    }

    routerEdge = bottleneckLink.Install(routers.Get(0), routers.Get(1));

    // Internet stack
    InternetStackHelper stack;
    stack.Install(sender);
    stack.Install(receiver);
    stack.Install(routers);

    // Queueing
    TrafficControlHelper tch;
    tch.SetRootQueueDisc(queueDisc);
    tch.SetQueueLimits("ns3::DynamicQueueLimits", "HoldTime", StringValue("1000ms"));

    tch.Install(routerEdge);
    
    // IP Addressing
    Ipv4AddressHelper ipv4;

    Ipv4InterfaceContainer senderInterfaces[senderCount];
    Ipv4InterfaceContainer receiverInterfaces[receiverCount];
    Ipv4InterfaceContainer routerInterface;
    
    for(int i = 0; i < cubicNodes; i++) {
        ns3::Ipv4Address ip(("10.1." + std::to_string(i) + ".0").c_str());
        ipv4.SetBase(ip, "255.255.255.0");
        senderInterfaces[i] = ipv4.Assign(senderEdges[i]);
    }

    
    for(int i = cubicNodes; i < senderCount; i++) {
        ns3::Ipv4Address ip(("10.2." + std::to_string(i-cubicNodes) + ".0").c_str());
        ipv4.SetBase(ip, "255.255.255.0");
        senderInterfaces[i] = ipv4.Assign(senderEdges[i]);
    }
    
    for(int i = 0; i < cubicNodes; i++) {
        ns3::Ipv4Address ip(("10.3." + std::to_string(i) + ".0").c_str());
        ipv4.SetBase(ip, "255.255.255.0");
        receiverInterfaces[i] = ipv4.Assign(receiverEdges[i]);
    }

    for(int i = cubicNodes; i < receiverCount; i++) {
        ns3::Ipv4Address ip(("10.4." + std::to_string(i-cubicNodes) + ".0").c_str());
        ipv4.SetBase(ip, "255.255.255.0");
        receiverInterfaces[i] = ipv4.Assign(receiverEdges[i]);
    }

    ipv4.SetBase("10.5.0.0", "255.255.255.0");
    routerInterface = ipv4.Assign(routerEdge);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    Ipv4GlobalRoutingHelper g;
    Ptr<OutputStreamWrapper> routingStream = Create<OutputStreamWrapper>
    ("dynamic-global-routing.routes", std::ios::out);
    g.PrintRoutingTableAllAt (Seconds (2), routingStream);

    ApplicationContainer sourceApps[senderCount], sinkApps[receiverCount];
    uint16_t port = 50001;

    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpCubic"));
    // LogComponentEnable("TcpCubic", LOG_LEVEL_ALL);

    for(int i=0; i<cubicNodes; i++) {
        std::cout<<"Creating bulk helper for "<<i<<std::endl;
        // Install application on the sender
        BulkSendHelper source("ns3::TcpSocketFactory", InetSocketAddress(receiverInterfaces[i].GetAddress(1), port));
        source.SetAttribute("MaxBytes", UintegerValue(0));
        sourceApps[i] = source.Install(sender.Get(i));
        sourceApps[i].Start(Seconds(0.0));
        sourceApps[i].Stop(stopTime);

        PacketSinkHelper sink("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
        sinkApps[i] = sink.Install(receiver.Get(i));
        sinkApps[i].Start(Seconds(0.0));
        sinkApps[i].Stop(stopTime);

        // Hook trace source after application starts - missing!!
    }

    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpBbr"));
    LogComponentEnable("TcpBbr", LOG_LEVEL_ALL);


    for(int i=cubicNodes; i<senderCount; i++) {
        std::cout<<"Creating bulk helper for "<<i<<std::endl;
        // Install application on the sender
        BulkSendHelper source("ns3::TcpSocketFactory", InetSocketAddress(receiverInterfaces[i].GetAddress(1), port));
        source.SetAttribute("MaxBytes", UintegerValue(0));
        sourceApps[i] = source.Install(sender.Get(i));
        sourceApps[i].Start(Seconds(0.0));

        // Hook trace source after application starts - missing!!
        sourceApps[i].Stop(stopTime);

        PacketSinkHelper sink("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
        sinkApps[i] = sink.Install(receiver.Get(i));
        sinkApps[i].Start(Seconds(0.0));
        sinkApps[i].Stop(stopTime);

        // Hook trace source after application starts - missing!!
    }

    // Track previous bytes sent by each sender
    for(int i=0; i<cubicNodes; i++) {
        prevCubic[i] = 0;
    }
    for(int i=0; i<bbrNodes; i++) {
        prevBbr[i] = 0;
    }

    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();   
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());

    Simulator::Schedule(Seconds(0), &TraceThroughput, monitor, classifier);

    Simulator::Stop(stopTime + TimeStep(1));

    Simulator::Run();
    Simulator::Destroy();


    tpPlot.AppendExtra(std::string("set xrange [0:+") + std::to_string(stopTime.GetSeconds()) + std::string("]"));
    tpPlot.AppendExtra(std::string("set yrange [0:+") + std::string("10]"));
    tpPlot.AppendExtra("set xlabel 'time (s)'");
    tpPlot.AppendExtra("set ylabel 'throughput (Mbps)'");
    tpPlot.SetTerminal("png");

    // rttPlot.AppendExtra(std::string("set xrange [0:+") + std::to_string(stopTime.GetSeconds()) + std::string("]"));
    // rttPlot.SetTerminal("png");

    for(int i=0; i<cubicNodes; i++) {
        cubicTpDataset[i].SetTitle("CUBIC" + std::to_string(i));
        cubicRttDataset[i].SetTitle("CUBIC" + std::to_string(i));
        cubicTpDataset[i].SetStyle(Gnuplot2dDataset::LINES);
        tpPlot.AddDataset(cubicTpDataset[i]);
        // rttPlot.AddDataset(cubicRttDataset[i]);
    }

    for(int i=0; i<bbrNodes; i++) {
        bbrTpDataset[i].SetTitle("BBR" + std::to_string(i));
        bbrRttDataset[i].SetTitle("BBR" + std::to_string(i));
        bbrTpDataset[i].SetStyle(Gnuplot2dDataset::LINES);
        tpPlot.AddDataset(bbrTpDataset[i]);
        // rttPlot.AddDataset(bbrRttDataset[i]);
    }

    std::ofstream tpPlotFile(expName + "-throughput.plt");
    tpPlot.GenerateOutput(tpPlotFile);
    
    // std::ofstream rttPlotFile(expName + "-rtt.plt");
    // rttPlot.GenerateOutput(rttPlotFile);

    // dataFile<<std::endl;

    tpPlotFile.close();
    // rttPlotFile.close();
}
