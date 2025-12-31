#include "mainwindow.h"
#include <QApplication>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QTranslator translator;
    QLocale locale = QLocale::system();

    if( locale.language() == QLocale::Chinese )
    {
       //ch:中文语言环境加载默认设计界面 | en:The Chinese language environment load the default design
    }
    else
    {
        //ch:其他语言环境加载英文界面 | en:Other language environments load the English design
        translator.load(QString(":/BasicDemoLineScan_zh_EN.qm")); //ch:选择翻译文件 | en:Choose the translation file
        a.installTranslator(&translator);
    }

    MainWindow w;


        // ================== 按钮字体加大 (11pt) + 布局安全适配 ==================
        QString qss = R"(
            /* ==================== 1. 全局基础 ==================== */
            QWidget {
                background-color: #1E1E1E;
                color: #F0F0F0;
                font-family: "Microsoft YaHei UI", "Microsoft YaHei", "Segoe UI", sans-serif;
                font-size: 10pt;                 /* 全局默认 10pt */
                font-weight: bold;
            }

            /* ==================== 2. 滚动条 ==================== */
            QScrollBar:vertical {
                border: none;
                background: #2D2D2D;
                width: 12px;
                margin: 0px;
            }
            QScrollBar::handle:vertical {
                background: #555555;
                min-height: 20px;
                border-radius: 6px;
            }
            QScrollBar::handle:vertical:hover { background: #007ACC; }
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
            QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }

            /* ==================== 3. 图像显示区 ==================== */
            QGraphicsView {
                background-color: #000000;
                border: 2px solid #3E3E42;
                border-radius: 4px;
            }
            QGraphicsView:hover { border: 2px solid #007ACC; }

            /* ==================== 4. 分组框 ==================== */
            QGroupBox {
                background-color: #252526;
                border: 1px solid #454545;
                border-radius: 6px;

                /* 顶部留出 26px，为可能被撑大的按钮留余地 */
                margin-top: 26px;
                padding-top: 6px;
                padding-bottom: 4px;
                padding-left: 2px;
                padding-right: 2px;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                subcontrol-position: top left;
                left: 8px;
                top: 2px;
                color: #4FC1FF;
                font-weight: 900;
                font-size: 11pt;                 /* 标题保持 11pt */
                background-color: transparent;
            }

            /* ==================== 5. 按钮 (字体加大至 11pt) ==================== */
            QPushButton {
                background-color: #333333;
                border: 1px solid #555555;
                border-radius: 4px;
                color: #FFFFFF;

                padding: 4px 6px;

                /* 【核心修改】按钮字体加大一级 -> 11pt */
                font-size: 12pt;

                min-height: 24px;                /* 稍微增加高度以容纳大字 */
            }
            /* 通用悬停 */
            QPushButton:hover { background-color: #454545; border-color: #999999; }
            QPushButton:pressed { background-color: #1E1E1E; border-color: #007ACC; }

            /* === 按钮颜色定义 (ID选择器) === */

            /* [绿色组] 运行/标定 */
            QPushButton#StartGrabbingButton, QPushButton#bnStartCalib9, QPushButton#bnStartCalculate {
                background-color: #1E6B2E; border: 1px solid #2E7D32;
            }
            QPushButton#StartGrabbingButton:hover, QPushButton#bnStartCalib9:hover, QPushButton#bnStartCalculate:hover {
                background-color: #2E8B57;
            }

            /* [红色组] 停止/关闭 */
            QPushButton#StopGrabbingButton, QPushButton#CloseButton {
                background-color: #8B1A1A; border: 1px solid #B22222;
            }
            QPushButton#StopGrabbingButton:hover, QPushButton#CloseButton:hover {
                background-color: #B22222;
            }

            /* [蓝色组] 功能操作 */
            QPushButton#OpenButton, QPushButton#bnFindCircle, QPushButton#bnFindBox,
            QPushButton#bnDynamicMeasure, QPushButton#bnRectify, QPushButton#EnumButton,
            QPushButton#GetParameterButton, QPushButton#SetParameterButton {
                background-color: #005A9E; border: 1px solid #0078D7;
            }
            QPushButton#OpenButton:hover,
            QPushButton#bnFindCircle:hover,
            QPushButton#bnFindBox:hover,
            QPushButton#bnDynamicMeasure:hover,
            QPushButton#bnRectify:hover,
            QPushButton#EnumButton:hover,
            QPushButton#GetParameterButton:hover,
            QPushButton#SetParameterButton:hover {
                background-color: #0078D7;
            }

            /* ==================== 6. 输入框 ==================== */
            QLineEdit, QComboBox {
                background-color: #121212;
                border: 1px solid #555555;
                border-radius: 3px;
                padding: 2px 4px;
                color: #00FF80;
                font-family: "Consolas", "Microsoft YaHei UI";
                font-size: 10pt;                 /* 输入框保持 10pt */
            }
            QLineEdit:focus, QComboBox:focus { border: 1px solid #00FF80; }
            QComboBox::drop-down { border: none; width: 18px; }
            QComboBox::down-arrow {
                border-top: 5px solid #FFFFFF;
                border-left: 5px solid transparent; border-right: 5px solid transparent;
                margin-right: 4px; margin-top: 1px;
            }

            /* ==================== 7. 数据显示标签 ==================== */
            QLabel {
                color: #CCCCCC;
                background: transparent;
                font-weight: bold;
            }

            /* 【长数据】坐标：9pt 紧凑 */
            QLabel#labelCircleCenter,   QLabel#labelCircleCenter_2, QLabel#labelCircleCenter_3,
            QLabel#labelCircleCenter_4, QLabel#labelCircleCenter_5, QLabel#labelCircleCenter_6,
            QLabel#labelCircleCenter_7, QLabel#labelCircleCenter_8, QLabel#labelCircleCenter_9,
            QLabel#labelCalibP1, QLabel#labelCalibP2, QLabel#labelCalibP3,
            QLabel#labelCalibP4, QLabel#labelCalibP5, QLabel#labelCalibP6,
            QLabel#labelCalibP7, QLabel#labelCalibP8, QLabel#labelCalibP9,
            QLabel#labelBoxP1, QLabel#labelBoxP2, QLabel#labelBoxP3, QLabel#labelBoxP4,
            QLabel#labelCoord
            {
                color: #00E676;
                font-family: "Microsoft YaHei UI", sans-serif;
                font-size: 9pt;
                font-weight: bold;
            }

            /* 【短数据】数值：11pt 特粗 */
            QLabel#labelCircleRadius,   QLabel#labelCircleRadius_2, QLabel#labelCircleRadius_3,
            QLabel#labelCircleRadius_4, QLabel#labelCircleRadius_5, QLabel#labelCircleRadius_6,
            QLabel#labelCircleRadius_7, QLabel#labelCircleRadius_8, QLabel#labelCircleRadius_9,
            QLabel#labelGray, QLabel#labelMeanGray,
            QLabel#labellength, QLabel#labelwidth
            {
                color: #00E676;
                font-family: "Consolas", monospace;
                font-size: 11pt;
                font-weight: 900;
            }

            /* 状态栏 FPS */
            QLabel#labelFps {
                font-size: 12pt;
                color: #FFD700;
            }
        )";
        w.setStyleSheet(qss);

    w.show();

    return a.exec();
}
