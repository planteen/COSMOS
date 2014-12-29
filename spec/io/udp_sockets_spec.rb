# encoding: ascii-8bit

# Copyright 2014 Ball Aerospace & Technologies Corp.
# All Rights Reserved.
#
# This program is free software; you can modify and/or redistribute it
# under the terms of the GNU General Public License
# as published by the Free Software Foundation; version 3 with
# attribution addendums as found in the LICENSE.txt

require 'spec_helper'
require 'cosmos/io/udp_sockets'

module Cosmos

  describe UdpWriteSocket do

    describe "initialize" do
      it "should create a socket" do
        udp = UdpWriteSocket.new('127.0.0.1', 8888)
        udp.peeraddr[2].should eql '127.0.0.1'
        udp.peeraddr[1].should eql 8888
        udp.close
        udp = UdpWriteSocket.new('224.0.1.1', 8888, 7777, '127.0.0.1', 3)
        udp.local_address.ip_port.should eql 7777
        udp.getsockopt(Socket::IPPROTO_IP, Socket::IP_MULTICAST_TTL).int.should eql 3
        IPAddr.new_ntoh(udp.getsockopt(Socket::IPPROTO_IP, Socket::IP_MULTICAST_IF).data).to_s.should eql "127.0.0.1"
        udp.close
      end
    end

    describe "write" do
      it "should write data" do
        udp_read  = UdpReadSocket.new(8888)
        udp_write = UdpWriteSocket.new('127.0.0.1', 8888)
        udp_write.write("\x01\x02",2.0)
        udp_read.read.should eql "\x01\x02"
        udp_read.close
        udp_write.close
      end

      it "should handle timeouts" do
        allow_any_instance_of(UDPSocket).to receive(:write_nonblock) { raise Errno::EWOULDBLOCK }
        expect(IO).to receive(:select).at_least(:once).and_return([], nil)
        udp_write = UdpWriteSocket.new('127.0.0.1', 8888)
        expect { udp_write.write("\x01\x02",2.0) }.to raise_error(Timeout::Error)
        udp_write.close
      end
    end

    describe "multicast" do
      it "should determine if a host is multicast" do
        UdpWriteSocket.multicast?('127.0.0.1').should be_falsey
        UdpWriteSocket.multicast?('224.0.1.1').should be_truthy
      end
    end

  end

  describe UdpReadSocket do

    describe "initialize" do
      it "should create a socket" do
        udp = UdpReadSocket.new(8888)
        udp.local_address.ip_address.should eql '0.0.0.0'
        udp.local_address.ip_port.should eql 8888
        udp.close
        udp = UdpReadSocket.new(8888, '224.0.1.1')
        IPAddr.new_ntoh(udp.getsockopt(Socket::IPPROTO_IP, Socket::IP_MULTICAST_IF).data).to_s.should eql "0.0.0.0"
        udp.close
      end
    end

    describe "read" do
      it "should read data" do
        udp_read  = UdpReadSocket.new(8888)
        udp_write = UdpWriteSocket.new('127.0.0.1', 8888)
        udp_write.write("\x01\x02",2.0)
        udp_read.read.should eql "\x01\x02"
        udp_read.close
        udp_write.close
      end

      it "should handle timeouts" do
        allow_any_instance_of(UDPSocket).to receive(:recvfrom_nonblock) { raise Errno::EWOULDBLOCK }
        expect(IO).to receive(:select).at_least(:once).and_return([], nil)
        udp_read = UdpReadSocket.new(8889)
        expect { udp_read.read(2.0) }.to raise_error(Timeout::Error)
      end
    end

  end
end
