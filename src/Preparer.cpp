#include <thread>
#include <chrono>
#include <atomic>

#include "Preparer.hpp"
#include "MySQLDriver.hpp"
#include "Message.hpp"
#include "tsqueue.hpp"

extern tsqueue<Message> tsQueue;

void Preparer::prepProgressPrint(unsigned int startTs, unsigned int total) const {

    auto t0 = std::chrono::high_resolution_clock::now();

    while (progressLoad) {
	std::this_thread::sleep_for (std::chrono::seconds(10));
	auto t1 = std::chrono::high_resolution_clock::now();
	auto secFromStart = std::chrono::duration_cast<std::chrono::seconds>(t1-t0).count();

	auto estTotalTime = ( insertProgress > 0 ? secFromStart / (
			static_cast<double> (insertProgress - startTs)  / total
			) : 0 );

	cout << std::fixed << std::setprecision(2)
	    << "Time: " << secFromStart << "sec, "
	    << "Total: " << total << " points, "
	    << "Progress: " << insertProgress
	    << ", % done: " <<
	    static_cast<double> (insertProgress - startTs) * 100 / (total)
	    << "%, "
	    << "Est total time: " << estTotalTime
	    << "sec, Left: " << estTotalTime - secFromStart
	    << endl;
    }

}

void Preparer::Prep(){

    DataLoader.CreateSchema();
    DataLoader.Prep();

    /* create thread printing progress */
    std::thread threadReporter(&Preparer::prepProgressPrint,
	this,
	0,
	Config::LoadHours * 3600 / 60 * Config::MaxOrgs * Config::MaxDevices);

    /* Populate the test table with data */
    for (unsigned int ts = Config::StartTimestamp; ts < Config::StartTimestamp + Config::LoadHours * 3600 ; ts += 60) {
//	cout << "Timestamp: " << ts << endl;

	auto orgsCnt = PGen->GetNext(Config::MaxOrgs, 0);
	auto devicesCnt = PGen->GetNext(Config::MaxDevices, 0);

	/* Devices loop */
	for (auto oc = 1; oc <= orgsCnt ; oc++) {
		for (auto dc = 1; dc <= devicesCnt ; dc++) {
			Message m(Insert, ts, oc, dc);
			tsQueue.push(m);
	//		cout << "# progress: " << oc << " +++ " << dc << endl;
			insertProgress = oc * dc * ((ts - Config::StartTimestamp)/60+ 1) ;
		}
	}

	tsQueue.wait_empty();


    }

    cout << "#\t Data Load Finished" << endl;
    progressLoad = false;
    /* wait on reporter to finish */
    threadReporter.join();

}

void Preparer::Run(){

    unsigned int minTs, maxTs;

    DataLoader.Run(minTs, maxTs);

    auto tsRange = maxTs - minTs;

    /* create thread printing progress */
    std::thread threadReporter(&Preparer::prepProgressPrint,
	this,
	maxTs + 60,
	Config::LoadHours * 3600);


    cout << "Running benchmark from ts: "
	<< maxTs + 60 << ", to ts: "
	<< maxTs + Config::LoadHours * 3600
	<< ", range: "
	<< tsRange
	<< endl;

    for (auto ts=maxTs + 60; ts < maxTs + Config::LoadHours * 3600; ts += 60) {

	auto orgsCnt = PGen->GetNext(Config::MaxOrgs, 0);
        unsigned int devicesCnt = PGen->GetNext(Config::MaxDevices, 0);
	unsigned int oldDevicesCnt = DataLoader.getMaxDevIdForTS(ts - tsRange - 60);

/*
	cout << "Timestamp: " << ts
	    << ", Devices: "
	    << devicesCnt
	    << ", Old Devices: "
	    << oldDevicesCnt
	    << endl;
*/
	/* Devices loop */
	for (auto oc = 1; oc <= orgsCnt ; oc++) {
	for (auto dc = 1; dc <= max(devicesCnt,oldDevicesCnt) ; dc++) {
	    if (dc <= devicesCnt) {
		Message m(Insert, ts, oc, dc);
		tsQueue.push(m);
	    }
	    if (dc <= oldDevicesCnt) {
		Message m(Delete, ts - tsRange - 60, oc, dc);
		tsQueue.push(m);
	    }
	}
	}

	tsQueue.wait_size(Config::LoaderThreads*10);
	insertProgress = ts;
    }

    cout << "#\t Benchmark Finished" << endl;
    progressLoad = false;
    /* wait on reporter to finish */
    threadReporter.join();
}

void Preparer::setLatencyStats(LatencyStats* ls)
{
  latencyStats=ls;
}

