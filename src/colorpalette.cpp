#include "colorpalette.h"
#include "i18n.h"
#include <QDialog>
#include <QColorDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMenu>
#include <QSettings>
#include <QStringList>
#include <QWidget>

namespace sticky {
namespace {
const QStringList &presets(bool text){
    static const QStringList body={
        "#222222","#555555","#777777","#FFFFFF","#80392D","#B04444","#BA662A","#A58A31",
        "#245C43","#33805B","#327E81","#285D9A","#3C5193","#7553A0","#9B4B83","#A84D62"
    };
    static const QStringList paper={
        "#FFF4D6","#FAECC0","#F6E6B7","#EED9A0","#F6E2C9","#EBC7A8","#FBE8E5","#F0C7CD",
        "#E6B9C6","#F4DAE8","#DDC1DF","#CFB4D5","#F0E8F8","#DCCFED","#C9C3E6","#D7E7F7",
        "#C4DBF2","#AFCEE9","#E8F4EC","#C3E4D5","#ADDBC9","#DFE8D0","#F8F5EF","#DCD8D0"
    };
    return text?body:paper;
}
QPushButton *colorButton(const QColor &c,QWidget *parent){
    auto *b=new QPushButton(parent);
    b->setFixedSize(32,32);
    b->setCursor(Qt::PointingHandCursor);
    b->setToolTip(c.name().toUpper());
    b->setStyleSheet(QString(
        "QPushButton{background:%1;border:1px solid #99A1A5;border-radius:5px;}"
        "QPushButton:hover{border:2px solid #3676A6;}").arg(c.name()));
    return b;
}
void remember(const QString &key,const QColor &color){
    QSettings settings;
    QStringList colors=settings.value(key).toStringList();
    const QString hex=color.name().toUpper();
    colors.removeAll(hex);colors.prepend(hex);
    while(colors.size()>4)colors.removeLast();
    settings.setValue(key,colors);
}
}
void showColorPalette(QWidget *parent,bool textPalette,const QColor &current,
                      const std::function<void(const QColor&)> &apply,
                      const std::function<void()> &reset){
    QDialog dlg(parent);
    dlg.setWindowTitle(textPalette?trUi("Text color"):trUi("Note color"));
    dlg.setMinimumWidth(364);
    auto *layout=new QVBoxLayout(&dlg);
    layout->setSpacing(9);
    auto *title=new QLabel(textPalette?trUi("Text colors"):trUi("Soft colors"),&dlg);
    QFont titleFont=title->font();
    titleFont.setPixelSize(13);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    layout->addWidget(title);
    const QString recentKey=textPalette?"recentTextColors":"recentPaperColors";
    const QString customKey=textPalette?"customTextColors":"customPaperColors";
    auto choose=[&](const QColor &c){
        remember(recentKey,c);apply(c);dlg.accept();
    };
    if(textPalette){
        auto *defaultButton=new QPushButton(trUi("Default text color"),&dlg);
        defaultButton->setToolTip(trUi("Remove the text color without changing other formatting"));
        layout->addWidget(defaultButton);
        QObject::connect(defaultButton,&QPushButton::clicked,&dlg,[&]{
            if(reset)reset();
            dlg.accept();
        });
    }
    auto *grid=new QGridLayout;
    grid->setSpacing(5);
    const auto &palette=presets(textPalette);
    for(int i=0;i<palette.size();++i){
        const QColor c(palette[i]);
        auto *button=colorButton(c,&dlg);
        grid->addWidget(button,i/8,i%8);
        QObject::connect(button,&QPushButton::clicked,&dlg,[&,c]{choose(c);});
    }
    layout->addLayout(grid);
    auto *customTitle=new QLabel(trUi("Custom colors (8)"),&dlg);
    QFont customTitleFont=customTitle->font();
    customTitleFont.setWeight(QFont::DemiBold);
    customTitle->setFont(customTitleFont);
    layout->addWidget(customTitle);
    auto *customGrid=new QGridLayout;
    customGrid->setSpacing(5);
    layout->addLayout(customGrid);
    std::function<void()> redraw;
    redraw=[&]{
        while(QLayoutItem *item=customGrid->takeAt(0)){
            if(item->widget())item->widget()->deleteLater();
            delete item;
        }
        QSettings settings;
        const auto saved=settings.value(customKey).toStringList().mid(0,8);
        for(int i=0;i<8;++i){
            if(i<saved.size()){
                const QColor c(saved[i]);
                auto *button=colorButton(c,&dlg);
                button->setToolTip(c.name().toUpper()+trUi(" — right-click to edit or remove"));
                button->setContextMenuPolicy(Qt::CustomContextMenu);
                customGrid->addWidget(button,0,i);
                QObject::connect(button,&QPushButton::clicked,&dlg,[&,c]{choose(c);});
                QObject::connect(button,&QWidget::customContextMenuRequested,&dlg,[&,i,button](const QPoint &pos){
                    QMenu menu(&dlg);
                    auto *edit=menu.addAction(trUi("Edit color…"));
                    auto *erase=menu.addAction(trUi("Remove color"));
                    const auto action=menu.exec(button->mapToGlobal(pos));
                    if(!action)return;
                    QSettings settings;
                    QStringList colors=settings.value(customKey).toStringList();
                    if(i>=colors.size())return;
                    if(action==edit){
                        const auto replacement=QColorDialog::getColor(QColor(colors[i]),&dlg,trUi("Edit custom color"));
                        if(!replacement.isValid())return;
                        colors[i]=replacement.name().toUpper();
                    }else if(action==erase){
                        colors.removeAt(i);
                    }
                    settings.setValue(customKey,colors);redraw();
                });
            }else if(i==saved.size()){
                auto *add=new QPushButton("+",&dlg);
                add->setFixedSize(32,32);
                add->setCursor(Qt::PointingHandCursor);
                add->setToolTip(trUi("Add a custom color"));
                customGrid->addWidget(add,0,i);
                QObject::connect(add,&QPushButton::clicked,&dlg,[&]{
                    const QColor picked=QColorDialog::getColor(current.isValid()?current:QColor("#F6E6B7"),
                        &dlg,trUi("Add custom color"));
                    if(!picked.isValid())return;
                    QSettings settings;
                    QStringList colors=settings.value(customKey).toStringList();
                    if(colors.size()>=8)return;
                    colors.append(picked.name().toUpper());
                    settings.setValue(customKey,colors);redraw();
                });
            }else{
                auto *placeholder=new QWidget(&dlg);
                placeholder->setFixedSize(32,32);
                placeholder->setStyleSheet("border:1px dashed #C4C4C4;border-radius:5px;");
                customGrid->addWidget(placeholder,0,i);
            }
        }
    };
    redraw();
    layout->addWidget(new QLabel(trUi("Recently used"),&dlg));
    auto *recentRow=new QHBoxLayout;recentRow->setSpacing(5);
    const auto recent=QSettings().value(recentKey).toStringList().mid(0,4);
    for(const auto &hex:recent){
        const QColor c(hex);
        if(!c.isValid())continue;
        auto *button=colorButton(c,&dlg);
        recentRow->addWidget(button);
        QObject::connect(button,&QPushButton::clicked,&dlg,[&,c]{choose(c);});
    }
    recentRow->addStretch();
    layout->addLayout(recentRow);
    auto *buttons=new QHBoxLayout;
    auto *advanced=new QPushButton(trUi("Advanced color…"),&dlg);
    auto *cancel=new QPushButton(trUi("Cancel"),&dlg);
    buttons->addWidget(advanced);buttons->addStretch();buttons->addWidget(cancel);
    layout->addLayout(buttons);
    QObject::connect(cancel,&QPushButton::clicked,&dlg,&QDialog::reject);
    QObject::connect(advanced,&QPushButton::clicked,&dlg,[&]{
        const QColor c=QColorDialog::getColor(current.isValid()?current:QColor("#272625"),
                                              &dlg,trUi("Advanced color"));
        if(c.isValid())choose(c);
    });
    dlg.exec();
}
}
