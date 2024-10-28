#include "DistrhoPlugin.hpp"
#include "extra/ValueSmoother.hpp"

#include "faust/dsp/poly-dsp.h"
#include "faust/midi/midi.h"
#include "faust/gui/PrintUI.h"
#include "faust/gui/APIUI.h"
#include "faust/gui/MidiUI.h"

#include "dsp.hpp"

std::list<GUI*> GUI::fGuiList;
ztimedmap GUI::gTimedZoneMap;

enum MidiStatus {
    // channel voice messages
    MIDI_NOTE_OFF = 0x80,
    MIDI_NOTE_ON = 0x90,
    MIDI_CONTROL_CHANGE = 0xB0,
    MIDI_PROGRAM_CHANGE = 0xC0,
    MIDI_PITCH_BEND = 0xE0,
    MIDI_AFTERTOUCH = 0xD0,         // aka channel pressure
    MIDI_POLY_AFTERTOUCH = 0xA0,    // aka key pressure
    MIDI_CLOCK = 0xF8,
    MIDI_START = 0xFA,
    MIDI_CONT = 0xFB,
    MIDI_STOP = 0xFC,
    MIDI_SYSEX_START = 0xF0,
    MIDI_SYSEX_STOP = 0xF7
};

struct FaustParam {
    int index;
    std::string label;
    std::string shortname;
    std::string address;
    float init;
    float min;
    float max;
    
    FaustParam(int index, std::string label, std::string shortname, std::string address, float init, float min, float max) :
        index(index),
        label(label),
        shortname(shortname),
        address(address),
        init(init),
        min(min),
        max(max) {
        
    }
    
    static bool isUserExposedParam(std::string label, std::string addr) {
        if ((label == "gate")
        || (label == "freq")
        || (label == "key")
        || (label == "gain")
        || (label == "vel")
        || (label == "velocity")
        || (addr == "/Polyphonic/Voices/Panic")) {
            return false;
        } else {
            return true;
        }
    }
};

class dpf_midi : public midi_handler {
public:
    void processMidiInBuffer(const MidiEvent* midiEvents, uint32_t midiEventCount)
    {
        for (size_t m = 0; m < midiEventCount; ++m) {
            
            MidiEvent event = midiEvents[m];
            size_t nBytes = event.size;
            int type = (int)event.data[0] & 0xf0;
            int channel = (int)event.data[0] & 0x0f;
            double time = event.frame; // Timestamp in frames

            // MIDI sync
            if (nBytes == 1) {
                handleSync(time, (int)event.data[0]);
            } else if (nBytes == 2) {
                handleData1(time, type, channel, (int)event.data[1]);
            } else if (nBytes == 3) {
                handleData2(time, type, channel, (int)event.data[1], (int)event.data[2]);
            } else {
                std::vector<unsigned char> message(event.data, event.data + event.size);
                handleMessage(time, type, message);
            }
        }
    }
    
    dpf_midi(const std::string& name = "DPFHandler")
        :midi_handler(name)
    {
    }
    virtual ~dpf_midi()
    {
    }
};

START_NAMESPACE_DISTRHO

// --------------------------------------------------------------------------------------------------------------------

static constexpr const float CLAMP(float v, float min, float max)
{
    return std::min(max, std::max(min, v));
}

static constexpr const float DB_CO(float g)
{
    return g > -90.f ? std::pow(10.f, g * 0.05f) : 0.f;
}

// --------------------------------------------------------------------------------------------------------------------

class FaustDPFPluginDSP : public Plugin
{
public:
    dsp* DSP;
    APIUI api;
    
    float** input_buffer;
    
    MidiUI* midiinterface;
    dpf_midi* dpfmidi;
    
    float oneOverSr;
    
    float* fPwm;
    
    std::vector<FaustParam> faustParameters;
    
    FaustDPFPluginDSP(int numParams)
        : Plugin(numParams, 0, 0) // parameters, programs, states
    {
        DSP = new mydsp_poly(new mydsp(), POLYPHONY, true, true);
        
        input_buffer = new float*[2];
        for (size_t i = 0; i < 2; i++) {
            input_buffer[i] = new float[4096];
        }
        
        DSP->buildUserInterface(&api);
        int realParamCount{0};
        auto paramCount = api.getParamsCount();
        d_stdout("getParamsCount: %d", paramCount);
        for (size_t p = 0; p < paramCount; p++) {
            std::string label = api.getParamLabel(p);
            std::string addr = api.getParamAddress(p);
            if (FaustParam::isUserExposedParam(label, addr)) {
                faustParameters.push_back(FaustParam(p, label, std::string(api.getParamShortname(p)), addr, api.getParamInit(p), api.getParamMin(p), api.getParamMax(p)));
                realParamCount++;
                d_stdout("%d -> %s (faust index %d)", realParamCount-1, api.getParamLabel(p), p);
            }
        }
        // d_stdout("%d params", realParamCount);
        
        if (realParamCount != numParams) {
            d_stdout("!!!!!!!!!!!!!");
            DSP = nullptr;
        }
    }

protected:
    const char* getLabel() const noexcept override { return "FaustDSPMidiDemo"; }
    const char* getDescription() const override { return ""; }
    const char* getMaker() const noexcept override { return ""; }
    const char* getLicense() const noexcept override { return ""; }
    uint32_t getVersion() const noexcept override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const noexcept override { return d_cconst('d', 'F', 's', 't'); }

    // ----------------------------------------------------------------------------------------------------------------
    // Init

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        parameter.ranges.min = faustParameters[index].min;
        parameter.ranges.max = faustParameters[index].max;
        parameter.ranges.def = faustParameters[index].init;
        parameter.hints = kParameterIsAutomatable;
        switch (api.getParamItemType(faustParameters[index].index)) {
                // parameter.hints |= kParameterIsTrigger;
                // break;
            case APIUI::kButton:
            case APIUI::kCheckButton:
                parameter.hints |= kParameterIsBoolean;
                break;
            case APIUI::kNumEntry:
                parameter.hints |= kParameterIsInteger;
                break;
            case APIUI::kHBargraph:
            case APIUI::kVBargraph:
            case APIUI::kVSlider:
            case APIUI::kHSlider:
                break;
        }
        parameter.name = faustParameters[index].address.c_str();
        parameter.shortName = faustParameters[index].shortname.c_str();
        parameter.symbol = faustParameters[index].shortname.c_str();
        parameter.unit = "";
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Internal data

    float getParameterValue(uint32_t index) const override
    {
        float ret = const_cast<APIUI&>(api).getParamValue(faustParameters[index].address.c_str());
        return ret;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        api.setParamValue(faustParameters[index].address.c_str(), value);
        GUI::updateAllGuis();
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Audio/MIDI Processing

    void activate() override
    {
        oneOverSr = 1/getSampleRate();
        
        mydsp_poly* dsp_poly = dynamic_cast<mydsp_poly*>(DSP);
        dsp_poly->init(getSampleRate());
        
        dpfmidi = new dpf_midi();
        midiinterface = new MidiUI(dpfmidi);
        dsp_poly->buildUserInterface(midiinterface);
    }

    void run(const float** inputs, float** outputs, uint32_t frames,
                     const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        dpfmidi->processMidiInBuffer(midiEvents, midiEventCount);
        
        if (midiEventCount) {
            GUI::updateAllGuis();
        }
        
        std::copy_n(inputs[0], frames, input_buffer[0]);
        std::copy_n(inputs[1], frames, input_buffer[1]);
        
        DSP->compute(frames, input_buffer, outputs);
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Callbacks (optional)

    void sampleRateChanged(double newSampleRate) override
    {
    }

    // ----------------------------------------------------------------------------------------------------------------

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FaustDPFPluginDSP)
};

// --------------------------------------------------------------------------------------------------------------------

Plugin* createPlugin()
{
    auto DSP = new mydsp_poly(new mydsp(), POLYPHONY, true, true);
    APIUI api;
    DSP->buildUserInterface(&api);
    
    auto UIParamsCount = api.getParamsCount();
    for (size_t p = 0; p < api.getParamsCount(); p++) {
        std::string label = api.getParamLabel(p);
        std::string addr = api.getParamAddress(p);
        if (!FaustParam::isUserExposedParam(label, addr)) { UIParamsCount--; }
    }
    
    delete DSP;
    
    return new FaustDPFPluginDSP(UIParamsCount);
}

// --------------------------------------------------------------------------------------------------------------------

END_NAMESPACE_DISTRHO
 