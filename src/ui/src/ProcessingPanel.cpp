#include <lumora/ui/ProcessingPanel.hpp>
#include <lumora/ui/ProcessingControlsModel.hpp>
#include <lumora/processing/ProcessingDefaults.hpp>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace lumora::ui {
namespace {
using Definition=processing::PipelineDefinition;
using Phase=ProcessingEditPhase;
// Keep full stored precision; short text avoids a column of trailing zeroes.
class NumericEntry final : public QDoubleSpinBox {
public:
    using QDoubleSpinBox::QDoubleSpinBox;
    QSize sizeHint() const override { auto size=QDoubleSpinBox::sizeHint(); size.setWidth(140); return size; }
    QSize minimumSizeHint() const override { auto size=QDoubleSpinBox::minimumSizeHint(); size.setWidth(100); return size; }
    QString textFromValue(double value) const override { return locale().toString(value,'g',QLocale::FloatingPointShortest); }
};
}
struct ProcessingPanel::Impl {
    ProcessingPanel& panel;
    ProcessingControlsModel& model;
    QVBoxLayout* layout;
    QComboBox* presets;
    QLabel* status;
    std::optional<core::Error> persistenceWarning;
    std::vector<std::function<void(const Definition&)>> synchronize;
    std::optional<application::PresetState> displayed;

    Impl(ProcessingPanel& owner,ProcessingControlsModel& state):panel(owner),model(state) {
        layout=new QVBoxLayout(&panel); layout->setContentsMargins(0,0,0,0); layout->setSpacing(8);
        auto* title=new QLabel(ProcessingPanel::tr("Processing"),&panel); layout->addWidget(title);
        auto* form=new QFormLayout; form->setRowWrapPolicy(QFormLayout::WrapLongRows); layout->addLayout(form);
        presets=new QComboBox(&panel); presets->setObjectName("presetSelector");
        presets->setAccessibleName(ProcessingPanel::tr("Processing preset"));
        presets->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        presets->setMinimumContentsLength(12);
        for(const auto& preset:model.presets()) {
            QString name=QString::fromStdString(preset.name);
            if(preset.id.value=="original") name=ProcessingPanel::tr("Original");
            else if(preset.id.value=="standard") name=ProcessingPanel::tr("Standard");
            else if(preset.id.value=="high-contrast") name=ProcessingPanel::tr("High Contrast");
            else if(preset.id.value=="soft-detail") name=ProcessingPanel::tr("Soft Detail");
            else if(preset.id.value=="custom") name=ProcessingPanel::tr("Custom");
            presets->addItem(name,QString::fromStdString(preset.id.value));
            presets->setItemData(presets->count()-1,QString::fromStdString(preset.description),Qt::ToolTipRole);
        }
        form->addRow(ProcessingPanel::tr("&Preset"),presets);
        QObject::connect(presets,&QComboBox::currentIndexChanged,&panel,[this] {
            (void)model.selectPreset({presets->currentData().toString().toStdString()}); changed();
        });
        status=new QLabel(&panel); status->setObjectName("processingSettingsStatus");
        status->setTextFormat(Qt::PlainText); status->setWordWrap(true);
        status->setAccessibleName(ProcessingPanel::tr("Processing settings status"));
        layout->addWidget(status);
    }
    void changed() { panel.refresh(); emit panel.edited(); }
    void mutate(const std::function<void(Definition&)>& edit,Phase phase=Phase::Commit) {
        auto definition=model.draft().activePipeline; edit(definition);
        (void)model.edit(std::move(definition),phase); changed();
    }
    QVBoxLayout* section(const QString& title,std::size_t stage) {
        auto* group=new QGroupBox(title,&panel); group->setCheckable(true);
        group->setObjectName(QStringLiteral("processingStage%1").arg(static_cast<qulonglong>(stage)));
        group->setAccessibleName(title); layout->addWidget(group);
        auto* contents=new QVBoxLayout(group); contents->setContentsMargins(8,8,8,8);
        synchronize.emplace_back([group,stage](const Definition& definition) {
            const QSignalBlocker blocker(group); group->setChecked(definition.stages[stage].enabled);
        });
        QObject::connect(group,&QGroupBox::toggled,&panel,[this,stage](bool enabled) {
            mutate([=](Definition& definition){definition.stages[stage].enabled=enabled;});
        });
        return contents;
    }
    using Getter=std::function<double(const Definition&)>;
    using Setter=std::function<void(Definition&,double)>;
    void numeric(QVBoxLayout* section,const char* name,const QString& label,double minimum,double maximum,
                 double step,Getter get,Setter set,bool slider=true,int decimals=17) {
        auto* row=new QFormLayout; row->setRowWrapPolicy(QFormLayout::WrapLongRows); section->addLayout(row);
        auto* entry=new NumericEntry(&panel); entry->setObjectName(QString::fromLatin1(name));
        entry->setDecimals(decimals); entry->setRange(minimum,maximum); entry->setSingleStep(step);
        entry->setKeyboardTracking(false);
        auto accessibleLabel=label; accessibleLabel.remove(QLatin1Char('&'));
        entry->setAccessibleName(accessibleLabel);
        entry->setMinimumWidth(90);
        const auto range=ProcessingPanel::tr("Range: %1 to %2").arg(minimum).arg(maximum);
        entry->setAccessibleDescription(range); entry->setToolTip(range);
        row->addRow(label,entry);
        QObject::connect(entry,&QDoubleSpinBox::valueChanged,&panel,[this,set](double value) {
            mutate([=](Definition& definition){set(definition,value);});
        });
        synchronize.emplace_back([entry,get](const Definition& definition) {
            const QSignalBlocker blocker(entry); entry->setValue(get(definition));
        });
        if(!slider) return;
        auto* control=new QSlider(Qt::Horizontal,&panel);
        control->setObjectName(QString::fromLatin1(name)+QStringLiteral("Slider"));
        control->setRange(0,1000); control->setAccessibleName(accessibleLabel);
        control->setAccessibleDescription(range+ProcessingPanel::tr(". Use numeric entry for exact values."));
        section->addWidget(control);
        QObject::connect(control,&QSlider::valueChanged,&panel,[this,set,minimum,maximum,control](int position) {
            const double value=minimum+(maximum-minimum)*static_cast<double>(position)/1000.0;
            mutate([=](Definition& definition){set(definition,value);},control->isSliderDown()?Phase::Drag:Phase::Commit);
        });
        QObject::connect(control,&QSlider::sliderReleased,&panel,[this] {
            (void)model.edit(model.draft().activePipeline,Phase::Release); changed();
        });
        synchronize.emplace_back([control,get,minimum,maximum](const Definition& definition) {
            const QSignalBlocker blocker(control);
            control->setValue(static_cast<int>(std::lround((get(definition)-minimum)/(maximum-minimum)*1000.0)));
        });
    }
    template<class Parameters>
    void number(QVBoxLayout* section,const char* name,const QString& label,std::size_t stage,
                double Parameters::* member,double minimum,double maximum,double step,bool slider=true) {
        numeric(section,name,label,minimum,maximum,step,
            [=](const Definition& definition){return std::get<Parameters>(definition.stages[stage].parameters).*member;},
            [=](Definition& definition,double value){std::get<Parameters>(definition.stages[stage].parameters).*member=value;},slider);
    }
    void denoise(QVBoxLayout* section) {
        using P=processing::DenoiseParameters;
        auto* form=new QFormLayout; form->setRowWrapPolicy(QFormLayout::WrapLongRows); section->addLayout(form);
        auto* mode=new QComboBox(&panel); mode->setObjectName("denoiseMode");
        mode->setAccessibleName(ProcessingPanel::tr("Denoise mode"));
        mode->addItem(ProcessingPanel::tr("Gaussian"),static_cast<int>(processing::DenoiseMode::Gaussian));
        mode->addItem(ProcessingPanel::tr("Median"),static_cast<int>(processing::DenoiseMode::Median));
        form->addRow(ProcessingPanel::tr("&Mode"),mode);
        auto* kernel=new QComboBox(&panel); kernel->setObjectName("denoiseKernel");
        kernel->setAccessibleName(ProcessingPanel::tr("Denoise kernel")); form->addRow(ProcessingPanel::tr("&Kernel"),kernel);
        QObject::connect(mode,&QComboBox::currentIndexChanged,&panel,[this,mode] {
            mutate([=](Definition& definition) {
                auto& parameters=std::get<P>(definition.stages[5].parameters);
                parameters.mode=static_cast<processing::DenoiseMode>(mode->currentData().toInt());
                if(parameters.mode==processing::DenoiseMode::Median) {
                    parameters.kernelSize=std::min(parameters.kernelSize,5U); parameters.sigma=0;
                }
            });
        });
        QObject::connect(kernel,&QComboBox::currentIndexChanged,&panel,[this,kernel] {
            mutate([=](Definition& definition){std::get<P>(definition.stages[5].parameters).kernelSize=kernel->currentData().toUInt();});
        });
        synchronize.emplace_back([mode,kernel](const Definition& definition) {
            const auto& parameters=std::get<P>(definition.stages[5].parameters);
            const QSignalBlocker modeBlock(mode),kernelBlock(kernel);
            mode->setCurrentIndex(mode->findData(static_cast<int>(parameters.mode)));
            kernel->clear(); for(unsigned value:{3U,5U,7U}) {
                if(value==7U && parameters.mode==processing::DenoiseMode::Median) break;
                kernel->addItem(QString::number(value),value);
            }
            kernel->setCurrentIndex(kernel->findData(parameters.kernelSize));
        });
        number(section,"denoiseSigma",ProcessingPanel::tr("&Sigma"),5,&P::sigma,0,5,0.1,false);
        auto* sigma=panel.findChild<QDoubleSpinBox*>("denoiseSigma");
        synchronize.emplace_back([sigma](const Definition& definition) {
            sigma->setEnabled(std::get<P>(definition.stages[5].parameters).mode==processing::DenoiseMode::Gaussian);
        });
    }
};
ProcessingPanel::ProcessingPanel(ProcessingControlsModel& model,QWidget* parent)
    :QWidget(parent),impl_(std::make_unique<Impl>(*this,model)) {
    setObjectName("processingPanel"); setAccessibleName(tr("Processing controls"));
    using namespace processing;
    auto& d=*impl_;
    auto* window=d.section(tr("Window / Level"),1);
    d.number(window,"window",tr("&Window"),1,&WindowLevelParameters::window,1,65535,100);
    d.number(window,"level",tr("&Level"),1,&WindowLevelParameters::level,0,65535,100);
    auto* brightness=d.section(tr("Brightness/Contrast"),2);
    d.number(brightness,"brightness",tr("&Brightness"),2,&BrightnessContrastParameters::brightness,-1,1,0.01);
    d.number(brightness,"contrast",tr("&Contrast"),2,&BrightnessContrastParameters::contrast,0,4,0.05);
    d.number(d.section(tr("Gamma"),3),"gamma",tr("&Gamma"),3,&GammaParameters::gamma,0.1,5,0.05);
    auto* clahe=d.section(tr("Local contrast"),4);
    clahe->parentWidget()->setToolTip(tr("Contrast Limited Adaptive Histogram Equalization (CLAHE)"));
    clahe->parentWidget()->setAccessibleName(tr("Local contrast (CLAHE)"));
    d.number(clahe,"claheClip",tr("Clip &limit"),4,&ClaheParameters::clipLimit,0.1,40,0.1);
    d.numeric(clahe,"claheGrid",tr("Tile &grid"),2,32,1,
        [](const Definition& definition){return static_cast<double>(std::get<ClaheParameters>(definition.stages[4].parameters).tileGridSize);},
        [](Definition& definition,double value){std::get<ClaheParameters>(definition.stages[4].parameters).tileGridSize=static_cast<std::uint32_t>(value);},false,0);
    d.denoise(d.section(tr("Denoise"),5));
    auto* sharpen=d.section(tr("Sharpen"),6);
    d.number(sharpen,"sharpenAmount",tr("&Amount"),6,&SharpenParameters::amount,0,5,0.1);
    auto* advanced=new QGroupBox(tr("Advanced"),this); advanced->setCheckable(true); advanced->setChecked(false);
    auto* advancedLayout=new QVBoxLayout(advanced); sharpen->addWidget(advanced);
    d.number(advancedLayout,"sharpenRadius",tr("&Radius"),6,&SharpenParameters::radius,0.5,5,0.1,false);
    d.number(advancedLayout,"sharpenThreshold",tr("&Threshold"),6,&SharpenParameters::threshold,0,65535,1,false);
    (void)d.section(tr("Invert"),7);
    auto* reset=new QPushButton(tr("Reset processing"),this); reset->setObjectName("resetProcessing");
    reset->setAccessibleName(tr("Reset processing to Original")); d.layout->addWidget(reset);
    connect(reset,&QPushButton::clicked,this,[this]{(void)impl_->model.reset();impl_->changed();});
    refresh();
}
ProcessingPanel::~ProcessingPanel()=default;
void ProcessingPanel::setPersistenceWarning(std::optional<core::Error> warning) {
    impl_->persistenceWarning=std::move(warning); refresh();
}
void ProcessingPanel::refresh() {
    auto& d=*impl_; const auto state=d.model.draft();
    if(!d.displayed || !processing::semanticallyEqualPipelineDefinitions(d.displayed->activePipeline,state.activePipeline)) {
        for(const auto& synchronize:d.synchronize) synchronize(state.activePipeline);
    }
    if(!d.displayed || d.displayed->selectedId!=state.selectedId) {
        const QSignalBlocker blocker(d.presets);
        d.presets->setCurrentIndex(d.presets->findData(QString::fromStdString(state.selectedId.value)));
    }
    d.displayed=state;
    if(d.model.error()) d.status->setText(QString::fromStdString(d.model.error()->operatorSummary));
    else if(d.persistenceWarning) d.status->setText(tr("Settings applied; saving failed: %1").arg(QString::fromStdString(d.persistenceWarning->operatorSummary)));
    else if(d.model.pending()) d.status->setText(tr("Changes pending…"));
    else if(d.model.acknowledged()) d.status->setText(tr("Settings applied"));
    else d.status->setText(tr("Waiting for processing session"));
}
}
