//
//  ServerThreads.cpp
//  Smarter Lock
//
//  Created by Yuwei Huang on 15/5/22.
//  Copyright (c) 2015年 CSE481. All rights reserved.
//

#define RSA_FILE "./private.pem"

#include "ServerThreads.h"

#include "Helpers.h"

#include "../Common/CommandPacket.h"
#include "../Common/PasscodePacket.h"
#include "../Common/VideoFramePacket.h"
#include "../Common/SimplePacket.h"

//#include "UDPCommunicator.h"
#include "../Common/TCPServer.h"

#include "../Common/GPIO.h"

#include <stdio.h>
#include <unistd.h>

#include <opencv2/highgui/highgui.hpp>

std::map<int, struct clientinfo> monitorMap;

static GPIO* io;

void unlock(Packet* up, CommunicationTask* ct) {
	CommandPacket accept = CommandPacket(Type::ACCEPT);
	accept.sequenceNumber = up->sequenceNumber;
	
	TCPServer::SendPacket(&accept, ct->sockfd_, true);
	TCPServer::CloseConnection(ct->sockfd_);
	
	printf("Door unlocked\n");
	io->write("27", true);
	sleep(1);
	io->write("27", false);
}

void passcode(Packet* up, CommunicationTask* ct) {
	char seq[17];
	seq[16] = '\0';
	Helpers::randSeq(seq);
	PasscodePacket pc = PasscodePacket(seq, 233333);
	pc.sequenceNumber = up->sequenceNumber;
	TCPServer::SendPacket(&pc, ct->sockfd_, true);
	TCPServer::CloseConnection(ct->sockfd_);
}

void startMonitor(Packet* up, CommunicationTask* ct) {
	int key = ct->addr_ + (up->sequenceNumber << 19);
	
	uint64_t vidKey = Helpers::rand64();
	
	SimplePacket sp = SimplePacket((uint8_t*)&vidKey, 8, Type::VIDEO_KEY, up->sequenceNumber);
	
	printf("Start streaming for fd %d, seqNum %d, video key %llu.\n", ct->sockfd_, up->sequenceNumber, vidKey);
	
	TCPServer::SendPacket(&sp, ct->sockfd_, true);
	
	monitorMap[key] = {up->sequenceNumber, ct->sockfd_, vidKey};
}


void stopMonitor(Packet* up, CommunicationTask* ct) {
	CommandPacket accept = CommandPacket(Type::ACCEPT);
	accept.sequenceNumber = up->sequenceNumber;
	
	TCPServer::SendPacket(&accept, ct->sockfd_, true);
	
	int key = ct->addr_ + (up->sequenceNumber << 19);
	struct clientinfo ci = monitorMap[key];
	
	printf("Stop streaming for fd %d.\n", ci.sockfd);
	TCPServer::CloseConnection(ci.sockfd);

	monitorMap.erase(key);
}

void ServerThreads::broadcastVideoFrame(cv::Mat frame) {
	if (TCPServer::IsRunning()) {
		std::vector<uchar> outbuf = std::vector<uchar>();
		cv::imencode(".jpg", frame, outbuf);
		
		
		for (auto i = monitorMap.begin(); i != monitorMap.end();) {
			struct clientinfo ci = i->second;
			
			VideoFramePacket vfp = VideoFramePacket(outbuf.data(), outbuf.size());
			vfp.sequenceNumber = ci.seqno;
			vfp.crypt(ci.vidkey);
			if (!TCPServer::SendPacket(&vfp, ci.sockfd)) {
				monitorMap.erase(i++);
				TCPServer::CloseConnection(ci.sockfd);
			} else
				++i;
			
		}
		outbuf.clear();
		
	}
}

void* ServerThreads::startServer(void* sth) {
	RSA* rsa = Helpers::rsaFromFile(RSA_FILE, false);

	TCPServer::RegisterCallback(Type::UNLOCK, unlock);
	TCPServer::RegisterCallback(Type::REQUEST_PASSCODE, passcode);
	TCPServer::RegisterCallback(Type::REQUEST_MONITOR, startMonitor);
	TCPServer::RegisterCallback(Type::STOP_MONITOR, stopMonitor);
	
	io = new GPIO();
	io->setup("27", GPIO::Direction::OUT);
	
	while (!TCPServer::Run(2333, 10, rsa)) {
		fprintf(stderr, "Failed to start server. wait 5 secs then retry.\n");
		sleep(5);
	}
	return nullptr;
}

void ServerThreads::cleanup() {
	printf("Received signal... Doing cleanups...\n");
		
	delete io;
	TCPServer::Kill();
	printf("Done\n");
}