/*
 * Copyright (c) 2013-2014,2016,2020 - Adjacent Link LLC, Bridgewater,
 * New Jersey
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of Adjacent Link LLC nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "harnessdownstreamtransport.h"
#include "utils.h"
#include "logservice.h"

#include "emane/configurationupdate.h"
#include "emane/utils/parameterconvert.h"
#include "emane/events/locationevent.h"
#include "emane/events/pathlossevent.h"

#include "emane/controls/rxantennaaddcontrolmessage.h"
#include "emane/controls/rxantennaremovecontrolmessage.h"
#include "emane/controls/mimotransmitpropertiescontrolmessage.h"
#include "emane/controls/mimotxwhilerxinterferencecontrolmessage.h"

#include <iostream>
#include <cstdlib>
#include <getopt.h>

namespace
{
  const EMANE::NEMId id{1};
  const std::uint16_t u16SubId{65535};
  const std::uint64_t u64BeginFrequencyHz{2000000000};
  const std::uint64_t u64BandwidthHz{1000000};

  void usage();

  std::tuple<std::vector<std::uint64_t>,EMANE::TimePoint>
  runOmni_processDownstreamPacket(EMANE::TimePoint start,
                                  std::size_t iterations,
                                  EMANE::FrameworkPHY * pPHYLayer,
                                  EMANE::NEMId src,
                                  EMANE::NEMId dst,
                                  const EMANE::Antennas & txAntennas,
                                  const EMANE::Antennas & rxAntennas,
                                  const EMANE::FrequencySet & frequencySet,
                                  bool bSelfInterference);
}



int main(int argc, char * argv[])
{
  option options[] =
    {
     {"help",0,nullptr,'h'},
     {"output",1,nullptr,'o'},
     {"processpoolsize",1,nullptr,'n'},
     {"selfinterference",0,nullptr,'s'},
     {0, 0,nullptr,0},
    };

  int iOption{};
  int iOptionIndex{};
  std::string sOutputFile{"output.csv"};
  std::uint16_t u16PoolSize{0};
  bool bSelfInterference{};
  bool bOmniTest{true};

  while((iOption = getopt_long(argc,argv,"ho:n:s", &options[0],&iOptionIndex)) != -1)
    {
      switch(iOption)
        {
        case 'h':
          // --help
          usage();
          return 0;

        case 'o':
          // --output
          sOutputFile = optarg;
          break;

        case 'n':
          u16PoolSize = EMANE::Utils::ParameterConvert{optarg}.toUINT16();
          break;

        case 's':
          bSelfInterference = true;
          break;

        case ':':
          // missing arguement
          std::cerr<<"-"<<static_cast<char>(iOption)<<"requires an argument"<<std::endl;
          return EXIT_FAILURE;

        default:
          std::cerr<<"Unknown option: "<<static_cast<char>(iOption)<<std::endl;
          return EXIT_FAILURE;
        }
    }


  std::cout.precision(10);

  try
    {
      const std::size_t iterations{100};
      const int iPriority{50};
      const bool bRealtime{true};
      const std::vector<std::size_t> frequencySegmentSteps{1,50,100,200,300,400,500};
      const std::vector<std::size_t> antennaCounts{1,2,4,8};

      std::vector<std::vector<std::uint64_t>> store{};

      std::vector<std::string> labels{};

      if(bRealtime)
        {
          struct sched_param schedParam{iPriority};

          if(sched_setscheduler(0,SCHED_RR,&schedParam))
            {
              std::cerr<<"unable to set realtime scheduler"<<std::endl;

              return EXIT_FAILURE;
            }
        }

      EMANE::LogService::instance()->setLogLevel(EMANE::ERROR_LEVEL);

      EMANE::SpectrumService spectrumService{};

      auto pPHYLayer = EMANE::Test::createPHY(id,&spectrumService);

      EMANE::Test::HarnessDownstreamTransport harnessDownstreamTransport{};

      pPHYLayer->setDownstreamTransport(&harnessDownstreamTransport);

      pPHYLayer->configure(EMANE::ConfigurationServiceSingleton::instance()->buildUpdates(pPHYLayer->getBuildId(),
                                                                                          {
                                                                                           {"compatibilitymode",{"2"}},
                                                                                           {"fixedantennagain",{"0.0"}},
                                                                                           {"fixedantennagainenable",{"on"}},
                                                                                           {"frequency",{std::to_string(u64BeginFrequencyHz)}},
                                                                                           {"bandwidth",{"1M"}},
                                                                                           {"noisemode",{"all"}},
                                                                                           {"propagationmodel",{"precomputed"}},
                                                                                           {"systemnoisefigure",{"7.0"}},
                                                                                           {"subid",{std::to_string(u16SubId)}},
                                                                                           {"processingpoolsize",{std::to_string(u16PoolSize)}},
                                                                                           {"fading.model",{"nakagami"}}}));;
      pPHYLayer->start();

      EMANE::Events::Pathlosses pathlosses;

      pathlosses.emplace_back(2,0,0);

      pPHYLayer->processEvent(EMANE::Events::PathlossEvent::IDENTIFIER,
                              EMANE::Events::PathlossEvent{pathlosses}.serialize());

      EMANE::Events::Locations locations;

      locations.emplace_back(1,
                             EMANE::Position{40.025495,-74.315441,3.0},

                             std::make_pair(EMANE::Orientation{0,32,30},true),
                             std::make_pair(EMANE::Velocity{60,-20,10},true));

      locations.emplace_back(2,
                             EMANE::Position{40.023235,-74.312889,3.0},
                             std::make_pair(EMANE::Orientation{},false),
                             std::make_pair(EMANE::Velocity{},false));

      pPHYLayer->processEvent(EMANE::Events::LocationEvent::IDENTIFIER,
                              EMANE::Events::LocationEvent{locations}.serialize());

      std::vector<EMANE::FrequencySet> frequencySets{};

      for(auto step : frequencySegmentSteps)
        {
          frequencySets.push_back(EMANE::Test::generateFrequencySet(step,
                                                                    u64BeginFrequencyHz,
                                                                    u64BandwidthHz));
        }

      std::size_t totalProcessed{};

      EMANE::Antennas txAntennas{};
      EMANE::Antennas rxAntennas{};
      EMANE::TimePoint start{};


      // ****************************
      if(bOmniTest)
        {
          for(const auto antennaCount : antennaCounts)
            {
              txAntennas.clear();
              rxAntennas.clear();

              for(std::size_t j = 0; j < antennaCount; ++j)
                {
                  auto antenna = EMANE::Antenna::createIdealOmni(j,0);

                  antenna.setBandwidthHz(u64BandwidthHz);

                  txAntennas.push_back(std::move(antenna));

                  rxAntennas.push_back(EMANE::Antenna::createIdealOmni(j,0));
                }

              for(const auto & frequencySet : frequencySets)
                {
                  auto ret = runOmni_processDownstreamPacket(start,
                                                             iterations,
                                                             pPHYLayer,
                                                             2,
                                                             id,
                                                             txAntennas,
                                                             rxAntennas,
                                                             frequencySet,
                                                             bSelfInterference);

                  store.push_back(std::move(std::get<0>(ret)));

                  labels.emplace_back("omni-" +
                                      std::to_string(txAntennas.size()) +
                                      "x" +
                                      std::to_string(rxAntennas.size()) +
                                      "-" +
                                      std::to_string(frequencySet.size()));

                  start = std::get<1>(ret);

                  totalProcessed += iterations;
                }
            }
        }

      // ****************************
      if(harnessDownstreamTransport.getTotalProcessed() != totalProcessed)
        {
          EMANE::Test::dumpDropTables(pPHYLayer->getBuildId());
        }

      std::ofstream fd{sOutputFile.c_str(), std::ios::out};

      if(fd)
        {
          for(std::size_t j = 0; j <labels.size(); ++j)
            {
              if(j)
                {
                  fd<<",";
                }

              fd<<labels[j];
            }
          fd<<std::endl;
          for(std::size_t j = 0; j < iterations; ++j)
            {
              for(std::size_t i = 0; i < store.size(); ++i)
                {
                  if(i)
                    {
                      fd<<",";
                    }

                  fd<<store[i][j];
                }

              fd<<std::endl;
            }
        }
    }
  catch(EMANE::Exception & exp)
    {
      std::cout<<"exception: "<<exp.what()<<std::endl;;
      return EXIT_FAILURE;
    }

  return EXIT_SUCCESS;
}

namespace
{
  void usage()
  {
    std::cout<<"usage: phyupstreamspeed [OPTIONS]..."<<std::endl;
    std::cout<<std::endl;
    std::cout<<"options:"<<std::endl;
    std::cout<<"  -h, --help                     Print this message and exit."<<std::endl;
    std::cout<<"  -o, --output CSVFILE           Name of output CSV file."<<std::endl;
    std::cout<<"                                   default: output.csv"<<std::endl;
    std::cout<<" -n, --processingpoolsize COUNT  Number of receive processor threads"<<std::endl;
    std::cout<<"                                   default: 0"<<std::endl;
    std::cout<<" -s, --selfinterference          Enable tx while rx interference."<<std::endl;
    std::cout<<std::endl;
  }


  std::tuple<std::vector<std::uint64_t>,EMANE::TimePoint>
  runOmni_processDownstreamPacket(EMANE::TimePoint start,
                                  std::size_t iterations,
                                  EMANE::FrameworkPHY * pPHYLayer,
                                  EMANE::NEMId src,
                                  EMANE::NEMId dst,
                                  const EMANE::Antennas & txAntennas,
                                  const EMANE::Antennas & rxAntennas,
                                  const EMANE::FrequencySet & frequencySet,
                                  bool bSelfInterference)
  {
    std::vector<std::uint64_t> store(iterations,0);
    std::vector<EMANE::AntennaIndex> rxAntennaIndexes{};

    EMANE::Antennas writeableTxAntennas{txAntennas};

    EMANE::Controls::MIMOTxWhileRxInterferenceControlMessage::RxAntennaInterferenceMap rxAntennaSelections{};

    for(const auto & rxAntenna : rxAntennas)
      {
        pPHYLayer->processDownstreamControl({EMANE::Controls::RxAntennaAddControlMessage::create(rxAntenna,
                                                                                                 frequencySet)});
        rxAntennaIndexes.push_back(rxAntenna.getIndex());

        rxAntennaSelections.emplace(rxAntenna.getIndex(),
                                    EMANE::Controls::AntennaSelfInterferences{EMANE::Controls::AntennaSelfInterference{0,.01}});
      }

    std::size_t size{1024};
    std::vector<std::uint8_t> data(size,0);

    EMANE::FrequencySegments segments{};

    for(auto u64FrequencyHz  : frequencySet)
      {
        segments.emplace_back(u64FrequencyHz,
                              EMANE::Microseconds{5000},
                              EMANE::Microseconds{0});
      }

    EMANE::FrequencyGroups frequencyGroups{std::move(segments)};

    EMANE::Transmitters transmitters{{src,0}};

    EMANE::TimePoint begin{};

    EMANE::TimePoint now{};

    for(std::size_t i = 0; i < iterations; ++i)
      {
        start += EMANE::Microseconds{5000};

        EMANE::DownstreamPacket pkt{{src,dst,0,start},&data[0],size};

        begin = EMANE::Clock::now();

        EMANE::ControlMessages msgs{EMANE::Controls::MIMOTransmitPropertiesControlMessage::create(frequencyGroups,
                                                                                                  txAntennas)};


        if(bSelfInterference)
          {
            msgs.push_back(EMANE::Controls::MIMOTxWhileRxInterferenceControlMessage::create(frequencyGroups,
                                                                                            rxAntennaSelections));
          }

        pPHYLayer->processDownstreamPacket_i(start,pkt,msgs);


        store[i] = std::chrono::duration_cast<EMANE::Microseconds>(EMANE::Clock::now() - begin).count();
      }

    for(const auto & rxAntennaIndex : rxAntennaIndexes)
      {
        pPHYLayer->processDownstreamControl({EMANE::Controls::RxAntennaRemoveControlMessage::create(rxAntennaIndex)});
      }

    return std::make_tuple(store,start);
  }
}
