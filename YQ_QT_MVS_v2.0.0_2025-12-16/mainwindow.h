#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "MvCamera.h"
#include <map>
#include <QMap>
#include <QDebug>
#include <opencv2/opencv.hpp>
#include <vector>
#include <QImage>
#include <QMutex>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QVector>
#include <QElapsedTimer>

#define MV_TRIGGER_SOURCE_EncoderModuleOut 6

#define STR_SOFTWARE "Software"
#define STR_FRAMEBURSTSTART "FrameBurstStart"

//模拟增益
typedef enum _MV_PREAMP_GAIN_
{
    GAIN_1000x = 1000,
    GAIN_1400x = 1400,
    GAIN_1600x = 1600,
    GAIN_2000x = 2000,
    GAIN_2400x = 2400,
    GAIN_4000x = 4000,
    GAIN_3200x = 3200,

}MV_PREAMP_GAIN;

//ImageCompressionMode模式
typedef enum _MV_IMAGE_COMPRESSION_MODE_
{
    IMAGE_COMPRESSION_MODE_OFF = 0,
    IMAGE_COMPRESSION_MODE_HB = 2,
}MV_IMAGE_COMPRESSION_MODE;

//触发选项
typedef enum _MV_CAM_TRIGGER_OPTION_
{
    FRAMEBURSTSTART = 6,
    LINESTART = 9,
}MV_CAM_TRIGGER_OPTION;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    void static __stdcall ImageCallBack2(MV_FRAME_OUT* pstFrame, void *pUser, bool bAutoFree);
    void ImageCallBackInner2(MV_FRAME_OUT* pstFrame, bool bAutoFree);

private:
    void ShowErrorMsg(QString csMessage, unsigned int nErrorNum); // ch:显示错误信息窗口 | en: Show the window of error message
    void EnableControls(bool bIsCameraReady);                   // ch:判断按钮使能 | en:Enable the controls

    int GetImageCompressionMode();                              // ch:获取图像压缩模式  | en:Get Image Compression Mode
    int GetTriggerSelector();                                   // ch:获取触发选项  | en:Get Trigger Selector
    int GetTriggerMode();                                       // ch:获取触发模式 | en:Get Trigger Mode
    int GetExposureTime();                                      // ch:获取曝光时间 | en:Get Exposure Time
    int SetExposureTime();                                      // ch:设置曝光时间 | en:Set Exposure Time
    int GetDigitalShiftGain();                                  // ch:获取数字增益 | en:Get Gain
    int SetDigitalShiftGain();                                  // ch:设置数字增益 | en:Set Gain
    int GetPreampGain();                                        // ch:获取模拟增益 | en:Get PreampGain
    int GetTriggerSource();                                     // ch:获取触发源 | en:Get Trigger Source
    int GetPixelFormat();                                       // ch:获取像素格式 | en:Get Pixel Format
    int GetAcquisitionLineRate();                               // ch:获取实际行频值 | en:Get Acquisition LineRate
    int SetAcquisitionLineRate();                               // ch:设置行频   | en:set Acquisition LineRate
    int GetAcquisitionLineRateEnable();                         // ch:获取行频使能开关 | en:Get Acquisition LineRate Enable
    int GetResultingLineRate();                                 // ch:获取实际行频 | en:Get Resulting LineRate

    int SaveImage(MV_SAVE_IAMGE_TYPE enSaveImageType);          // ch:保存图片 | en:Save Image

    int CloseDevice();                                          // ch:关闭设备 | en:Close Device

    void updateCalibLabels();          // 根据 m_calibPoints 刷新 labelCalibP1~P9

    // ch: 将一张 QImage 注入为“当前帧” | en: Use a QImage as the current frame
    void loadImageAsCurrentFrame(const QImage &srcImg);

    //ROI util function
    QRectF currentRoiRect() const;

    void clearAllOverlays();   // 清除图片上的所有痕迹，只保留原始帧

    // === ✨ 新增：实时测量功能相关变量 ===
     bool m_isRealTimeMeasure = false;   // 是否开启实时测量
     float m_pixelsPerCm = 0.0f;         // 标定系数：多少像素对应 1 cm (或mm)

     // 核心算法函数：输入图像，计算并在 Painter 上绘制尺寸
     void processAndDrawMeasurements(QPainter &painter, const QImage &img);

//signals:
//    // ✨ 新增：回调线程 -> UI 线程，用 QImage 传图
//    void sigNewFrame(const QImage &img);
signals:
    void sigNewFrame(const QImage &img);


private slots:
    void on_EnumButton_clicked();

    void on_OpenButton_clicked();

    void on_GetParameterButton_clicked();

    void on_StartGrabbingButton_clicked();

    void on_CloseButton_clicked();

    void on_StopGrabbingButton_clicked();

    void on_SetParameterButton_clicked();

    void on_SelchangeTriggerselCombo_currentTextChanged(const QString &arg1);

    void on_SelchangeTriggerswitchCombo_currentTextChanged(const QString &arg1);

    void on_SelchangeTriggersourceCombo_currentTextChanged(const QString &arg1);

    void on_SoftwareOnceButton_clicked();


    void on_SelchangePixelformatCombo_currentTextChanged(const QString &arg1);

    void on_SelchangeImageCompressionModeCombo_currentTextChanged(const QString &arg1);

    void on_ExposureTimeLineEdit_editingFinished();

    void on_SelchangePreampgainCombo_currentTextChanged(const QString &arg1);

    void on_PreampGainLineEdit_editingFinished();

    void on_AcquisitionLineRateLineEdit_editingFinished();

    void on_AcquisitionLineRateEnableCheckBox_clicked(bool checked);

    void on_SaveBmpButton_clicked();

    void on_SaveJpgButton_clicked();

    void on_SaveTiffButton_clicked();

    void on_SavePngButton_clicked();

    void on_bnFindCircle_clicked();   // ✨ 新增：找圆心按钮

    void onNewFrame(const QImage &img);  // 用于更新 QGraphicsView

    void on_bnFindBox_clicked();   // ✨ 新增：找盒子四点

    void on_bnRectify_clicked();   // ✨ 新增：畸变矫正


    void on_bnStartCalib9_clicked();   // 开始/进入九点标定
    void on_bnClearCalib9_clicked();   // 清除当前所有标定点
    void on_bnSaveCalib9_clicked();    // 保存九点标定结果

    void on_btnOpenImage_clicked();  //open image button

    void on_bnClearOverlay_clicked();
    // ✨ 新增：UI 交互槽函数
    void on_chkRealTimeMeasure_clicked(bool checked);
    void on_linePixelsPerCm_editingFinished();


//protected:
//    bool eventFilter(QObject *watched, QEvent *event) override;   // 新增:显示鼠标悬停处行列值、灰度值

protected:
    bool eventFilter(QObject *watched, QEvent *event) override; // 新增:显示鼠标悬停处行列值、灰度值


private:
    Ui::MainWindow *ui;

    void *m_hWnd;                                               // ch:显示窗口句柄 | en:The Handle of Display Window

    MV_CC_DEVICE_INFO_LIST  m_stDevList;                        // ch:设备信息链表 | en:The list of device info

    CMvCamera*              m_pcMyCamera;                       // ch:相机类设备实例 | en:The instance of CMvCamera

    bool                    m_bGrabbing;                        // ch:是否开始抓图 | en:The flag of Grabbing
    bool                    m_bOpenDevice;                      // ch:是否打开设备 | en:Whether to open device
    int                     m_nTriggerMode;                     // ch:触发模式 | en:Trigger Mode
    int                     m_nTriggerSelector;                 // ch:触发选项 | en:Trigger Selector
    int                     m_nTriggerSource;                   // ch:触发源 | en:Trigger Source
    int                     m_nPreampGain;                      // ch:模拟增益 | en:Preamp Gain
    int                     m_nPixelFormat;                     // ch:像素格式 | en:PixelFormat
    int                     m_nImageCompressionMode;            // ch:图像压缩模式 | en:ImageCompressionMode
    float                   m_dExposureEdit;                    // ch:图像曝光 | en:Image Exposure
    float                   m_dDigitalShiftGainEdit;            // ch:数字增益 | en:Digital Shift Gain
    int                     m_nAcquisitionLineRateEdit;         // ch:行频 | en:AcquisitionLineRate
    unsigned int            m_nSaveImageBufSize;                // ch:保存图像时缓冲区大小 | en:The size of SaveImageBuf
    QString  m_saveDir;     // 保存目录
    bool                    m_bTriggerModeCheck;                // ch:触发模式开启/关闭标志位 | en:The flag of TriggerMode On/Off
    bool                    m_bPreampGain;                      // ch:模拟增益使能标志位 | en:The flag of PreampGain
    bool                    m_bAcquisitionLineRate;             // ch:行频使能标志位 | en:The flag of AcquisitionLineRate
    bool                    m_bHBMode;                          // ch:是否无损压缩模式 | en: HB Mode On/Off
    unsigned char*          m_pSaveImageBuf;                    // ch:保存图像缓冲区 | en: The Buffer of SaveImage
    MV_FRAME_OUT_INFO_EX    m_stImageInfo;                      // ch:图像信息结构体 | en: The image info

    pthread_mutex_t        m_hSaveImageMux;                     // ch:保存图像锁 | en: The Mutex of SaveImage


    QMap<QString, int> m_mapPixelFormat;                        // ch:像素格式字符串-值 map | The map of PixelFormat value
    QMap<QString, int> m_mapPreampGain;                         // ch:模拟增益字符串-值 map | The map of PreampGain value
    QMap<QString, int> m_mapTriggerSource;                      // ch:触发源字符串-值   map | The map of TriggerSource value

    // 显示相关
    QGraphicsScene*         m_scene = nullptr;
    QGraphicsPixmapItem*    m_pixItem = nullptr;
    QImage                  m_displayImage;

    // ====== 新增：当前帧缓存，用于保存/取灰度 ======
    std::vector<unsigned char> m_frameBuffer;   // 当前帧的原始数据
    MV_FRAME_OUT_INFO_EX       m_frameInfo{};   // 当前帧的宽、高、像素类型等
    bool                       m_hasFrame = false;
    QMutex                     m_frameMutex;    // 保护上面这两个


    // 最近一次检测到的矩形四点（图像坐标）
    std::vector<cv::Point2f> m_lastQuad;
    bool m_hasQuad = false;

    // === 九点标定相关 ===
    bool m_isCalibrating = false;          // 是否处于“九点标定模式”
    QVector<cv::Point2f> m_calibPoints;    // 已经选取的标定点（图像坐标）

    //给“九点标定”加一层防呆保护
    int   m_calibImgWidth  = 0;            // 开始标定时的图像宽
    int   m_calibImgHeight = 0;            // 开始标定时的图像高
    float m_calibMinDist   = 20.0f;        // 标定点之间的最小间距（像素）


    // === 圆检测叠加 ===
    bool        m_hasCircleOverlay = false;
    cv::Point2f m_circleCenterOverlay;
    float       m_circleRadiusOverlay = 0.0f;


    // === 盒子四点叠加 ===
    bool                 m_hasBoxOverlay = false;
    QVector<cv::Point2f> m_boxPointsOverlay;   // 按 左上/右上/左下/右下 排序的 4 点


    // === 实时显示 FPS + 平均灰度 ===
    QElapsedTimer m_fpsTimer;
    int   m_frameCountForFps = 0;
    double m_currentFps      = 0.0;

    //交互式两点测距 (鼠标拖拽测量)
    bool m_isMeasuring = false;        // 是否正在测量模式
    bool m_isDraggingMeasure = false;  // 是否正在拖拽鼠标划线
    cv::Point2f m_measureStart;        // 测量起点
    cv::Point2f m_measureEnd;          // 测量终点


    // ROI 框选相关
    bool        m_hasRoi      = false;   // 是否有有效 ROI
    bool        m_isSelectingRoi = false;// 是否正在拖动选择 ROI
    cv::Point2f m_roiStart;              // ROI 起点（图像坐标）
    cv::Point2f m_roiEnd;                // ROI 终点（图像坐标）
};

#endif // MAINWINDOW_H
