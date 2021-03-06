#include <cmath>
#include <atomic>
#include <fe/SynchronizedFrontend.hxx>
#include <util/FrontEndUtils.hxx>
#include <util/TriggerInfoRawData.hxx>
#include <util/TDcOffsetRawData.hxx>
#include <util/TWaveFormRawData.hxx>
#include <util/caen/V1720InfoRawData.hxx>
#include <util/VectorComparator.hxx>
#include <midas/odb.hxx>
#include "defaults.hxx"

#define EQUIP_NAME "sinus"
constexpr int EVID = 1;
constexpr auto PI = 3.1415926536;
constexpr std::size_t NUM_OF_CHANNELS = 8;

#ifndef NEED_NO_EXTERN_C
extern "C" {
#endif

/* The frontend name (client name) as seen by other MIDAS clients   */
const char *frontend_name = "fe-" EQUIP_NAME;

/* The frontend file name, don't change it */
const char *frontend_file_name = __FILE__;

/* frontend_loop is called periodically if this variable is TRUE    */
BOOL frontend_call_loop = TRUE;

/* a frontend status page is displayed with this frequency in ms */
INT display_period = 1000;

/* maximum event size produced by this frontend */
INT max_event_size = 4 * 1024 * 1024;
INT max_event_size_frag = 4 * 1024 * 1024;

/* buffer size to hold events */
INT event_buffer_size = 10 * 1024 * 1024;

INT frontend_init();
INT frontend_exit();
INT begin_of_run(INT run_number, char *error);
INT end_of_run(INT run_number, char *error);
INT pause_run(INT run_number, char *error);
INT resume_run(INT run_number, char *error);
INT frontend_loop();
int create_event_rb(int i);
int get_event_rb(int i);
extern int stop_all_threads;
INT poll_event(INT source, INT count, BOOL test);
INT interrupt_configure(INT cmd, INT source, PTYPE adr);

extern HNDLE hDB;

int readEvent(char *pevent, int off);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

EQUIPMENT equipment[] = { { EQUIP_NAME, { EVID, (1 << EVID), /* event ID, trigger mask */
"SYSTEM", /* event buffer */
EQ_MULTITHREAD, /* equipment type */
0, /* event source */
"MIDAS", /* format */
TRUE, /* enabled */
RO_RUNNING, /* Read when running */
10, /* poll every so milliseconds */
0, /* stop run after this event limit */
0, /* number of sub events */
0, /* no history */
"", "", "" }, readEvent, /* readout routine */
}, { "" } };

#pragma GCC diagnostic pop

#ifndef NEED_NO_EXTERN_C
}
#endif

extern "C" {
void set_rate_period(int ms);
}

class SinusFrontend: public fe::SynchronizedFrontend {
public:

	SinusFrontend() :
			acquisitionIsOn(false), prevYieldTime(0) {
	}

private:

	std::atomic_bool acquisitionIsOn;
	uint64_t prevYieldTime;
	uint32_t recordLength = 0;
	midas_thread_t readoutThread;
	std::mutex readingMutex;
	uint64_t runStartTime;
	uint32_t eventCounter = 0;
	uint32_t channelMask = 0x0000;
	uint32_t dFrequency;

	std::vector<bool> enabledChannels;
	std::vector<uint16_t> dcOffsets;
	std::vector<uint16_t> amplitudes;
	std::vector<uint32_t> frequencies;
	std::vector<int32_t> phases;

	void configure() {

		auto const hSet = util::FrontEndUtils::settingsKey(EQUIP_NAME);

		dFrequency = odb::getValueUInt32(hDB, hSet, "discrete_frequency",
				defaults::dFrequency, true);
		recordLength = odb::getValueUInt32(hDB, hSet, "waveform_length",
				defaults::recordLength, true);

		enabledChannels = odb::getValueBoolV(hDB, hSet, "channel_enabled",
				NUM_OF_CHANNELS, true, true);
		dcOffsets = odb::getValueUInt16V(hDB, hSet, "channel_dc_offset",
				NUM_OF_CHANNELS, defaults::channel::dcOffset, true);
		frequencies = odb::getValueUInt32V(hDB, hSet, "channel_frequency",
				NUM_OF_CHANNELS, defaults::channel::frequency, true);
		amplitudes = odb::getValueUInt16V(hDB, hSet, "channel_amplitude",
				NUM_OF_CHANNELS, defaults::channel::amplitude, true);
		phases = odb::getValueInt32V(hDB, hSet, "channel_phase",
				NUM_OF_CHANNELS, defaults::channel::phase, true);

		channelMask = 0x0000;
		for (uint8_t i = 0; i < NUM_OF_CHANNELS; i++) {
			if (enabledChannels[i]) {
				channelMask |= 0x0001 << i;
			}
		}

	}

	void configureInRuntime() {
		auto const hSet = util::FrontEndUtils::settingsKey(EQUIP_NAME);
		auto const currentDcOffsets = odb::getValueUInt16V(hDB, hSet,
				"channel_dc_offset", NUM_OF_CHANNELS,
				defaults::channel::dcOffset, true);
		if (!util::VectorComparator::equal(currentDcOffsets, dcOffsets)) {
			cm_msg(MINFO, frontend_name, "DC offset changed");
			dcOffsets = currentDcOffsets;
		}

	}

	static uint64_t msTime() {

		auto const now = std::chrono::system_clock::now();
		auto const duration = now.time_since_epoch();
		return std::chrono::duration_cast < std::chrono::milliseconds
				> (duration).count();
	}

	static uint64_t nanoTime() {

		std::chrono::time_point < std::chrono::system_clock > now =
				std::chrono::system_clock::now();
		auto duration = now.time_since_epoch();
		auto nanoseconds = std::chrono::duration_cast < std::chrono::nanoseconds
				> (duration);
		return nanoseconds.count();

	}

	void startAcquisition() {

		runStartTime = nanoTime();
		eventCounter = 0;
		acquisitionIsOn.store(true);

	}

	void stopAcquisition() {

		acquisitionIsOn.store(false);

		std::lock_guard < std::mutex > lock(readingMutex);

	}

	int buildEvent(char * const pevent) {

		bk_init32(pevent);

		{
			// store general information
			uint8_t* pdata;
			bk_create(pevent, util::caen::V1720InfoRawData::bankName(),
					TID_DWORD, (void**) &pdata);
			util::InfoBank* info = (util::InfoBank*) pdata;
			info->boardId = 0;
			info->channelMask = channelMask;
			info->eventCounter = ++eventCounter;
			auto const t = nanoTime();
			info->timeStamp = t;
			info->pattern.raw = 0;
			info->frontendIndex = util::FrontEndUtils::frontendIndex<
					decltype(info->frontendIndex)>();
			bk_close(pevent, pdata + sizeof(*info));
		}

		{
			// store channel DC offset
			uint16_t* pdata;
			bk_create(pevent, util::TDcOffsetRawData::BANK_NAME, TID_WORD,
					(void**) &pdata);
			for (uint8_t i = 0; i < NUM_OF_CHANNELS; i++) {
				*pdata++ = dcOffsets[i];
			}
			bk_close(pevent, pdata);
		}

		// store wave forms
		auto const t = nanoTime() - runStartTime;
		for (uint8_t i = 0; i < NUM_OF_CHANNELS; i++) {
			if (enabledChannels[i]) {
				auto const dt = 1.0e9 / frequencies[i];

				if (recordLength > 0) {
					uint16_t* pdata;
					bk_create(pevent, util::TWaveFormRawData::bankName(i),
							TID_WORD, (void**) &pdata);

					for (uint32_t j = 0; j < recordLength; j++) {
						auto const ns = static_cast<int64_t>(j) * 1000000000
								/ dFrequency + t - phases[i];
						auto const v = std::sin(2.0 * PI * ns / dt)
								* amplitudes[i] + 2048;
						*pdata++ = v < 0.0 ? 0 :
									v > 4095 ? 4095 : static_cast<uint16_t>(v);
					}

					bk_close(pevent, pdata);
				}
			}
		}

		return bk_size(pevent);

	}

protected:

	void doInitSynchronized() override {

		// create subtree
		odb::getValueUInt32(hDB, 0,
				util::FrontEndUtils::settingsKeyName(EQUIP_NAME,
						"waveform_length"), defaults::recordLength, true);

		configure();

	}

	void doExitSynchronized() override {

		stopAcquisition();

	}

	void doBeginOfRunSynchronized(INT /* run_number */, char* /* error */)
			override {

		configure();
		startAcquisition();

	}

	void doEndOfRunSynchronized(INT /* run_number */, char* /* error */)
			override {

		stopAcquisition();

	}

	void doPauseRunSynchronized(INT /* run_number */, char* /* error */)
			override {

		stopAcquisition();

	}

	void doResumeRunSynchronized(INT /* run_number */, char* /* error */)
			override {

		startAcquisition();
	}

	int doPollSynchronized() override {

		doLoopSynchronized();
		ss_sleep(equipment[0].info.period);
		return TRUE;

	}

	int doReadEventSynchronized(char* const pevent, int /* off */) override {

		int result;

		if (acquisitionIsOn.load(std::memory_order_relaxed)) {
			result = buildEvent(pevent);
		} else {
			result = 0;
		}

		return result;

	}

	void doLoopSynchronized() override {
		auto const now = msTime();

		if (now - prevYieldTime >= defaults::yieldPeriod) {
			configureInRuntime();
			prevYieldTime = now;
		}
	}

};

static SinusFrontend frontend;

INT frontend_init() {

	return frontend.frontendInit();

}

INT frontend_exit() {

	return frontend.frontendExit();

}

INT begin_of_run(INT const run_number, char* const error) {

	return frontend.beginOfRun(run_number, error);

}

INT end_of_run(INT const run_number, char* const error) {

	return frontend.endOfRun(run_number, error);

}

INT pause_run(INT const run_number, char* const error) {

	return frontend.pauseRun(run_number, error);

}

INT resume_run(INT const run_number, char* const error) {

	return frontend.resumeRun(run_number, error);

}

INT frontend_loop() {

	return frontend.frontendLoop();

}

INT poll_event(INT const source, INT const count, BOOL const test) {

	return frontend.pollEvent(source, count, test);

}

INT interrupt_configure(INT const cmd, INT const source, PTYPE const adr) {

	return frontend.interruptConfigure(cmd, source, adr);

}

int readEvent(char * const pevent, int const off) {

	return frontend.readEvent(pevent, off);

}
