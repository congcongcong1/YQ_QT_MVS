#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QStyleFactory>
#include <QTextCodec>
#include <QDir>
#include <QFileDialog>
#include <QDateTime>
#include <opencv2/opencv.hpp>
#include <QEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QColor>
#include <QMutexLocker>
#include <algorithm>   // sort
#include <vector>
#include <QFile>
#include <QTextStream>
using namespace cv;


/**
 * @brief 改进后的角点排序：左上, 右上, 右下, 左下
 * @details 基于质心角度排序，抗旋转能力更强
 */
static void sortRectPoints(std::vector<cv::Point2f> &pts)
{
    if (pts.size() != 4) return;

    // 1. 计算质心
    cv::Point2f center(0, 0);
    for (const auto &p : pts) center += p;
    center *= (1.0 / pts.size());

    // 2. 根据相对于质心的角度排序 (atan2 返回值范围 -PI ~ PI)
    // sort 结果顺序一般为：左上(第三象限附近), 右上, 右下, 左下（逆时针或顺时针取决于坐标系）
    // 为了确保输出是 TL, TR, BR, BL，我们先按 y 坐标简单区分上下，再精细化
    // 这里使用一种更加工程化的方法：
    // 左上：x+y 最小； 右下：x+y 最大
    // 右上：x-y 最大； 左下：x-y 最小 (在图像坐标系下，y向下为正)
    // 注意：这种 summation 方法只适用于旋转角度不大的情况。
    // 为了支持任意角度，我们还是用 atan2 排序，然后找到左上角作为起始点。

    std::sort(pts.begin(), pts.end(), [center](const cv::Point2f &a, const cv::Point2f &b) {
        double angleA = std::atan2(a.y - center.y, a.x - center.x);
        double angleB = std::atan2(b.y - center.y, b.x - center.x);
        return angleA < angleB;
    });

    // 此时 pts 是按角度排序的。我们需要找到“左上角”。
    // 一般来说，左上角的点距离 (0,0) 最近，或者 Sum(x+y) 最小
    // 但在旋转矩形中，我们通常定义：先找最上面的点，如果有两个，取左边那个
    // 为保持和你原有逻辑一致（P1:左上, P2:右上, P3:右下, P4:左下），我们需要调整顺序

    // 简单健壮方案：
    // 1. 先计算四个点距离 (0,0) 的距离（或者 Sum(x+y)），最小的作为 "近似左上"
    // 2. 但为了彻底解决旋转问题，通常通过透视变换时的对应关系来约束。
    // 这里提供一个通用的“顺时针排序，以左上为起点”的方法：

    // 重排：先按 y 排序找到最上面的两个点
    std::vector<cv::Point2f> topPts, botPts;
    std::sort(pts.begin(), pts.end(), [](const cv::Point2f &a, const cv::Point2f &b){
        return a.y < b.y;
    });
    topPts.push_back(pts[0]); topPts.push_back(pts[1]);
    botPts.push_back(pts[2]); botPts.push_back(pts[3]);

    // topPts 中 x 小的是左上(TL)，大的是右上(TR)
    if (topPts[0].x > topPts[1].x) std::swap(topPts[0], topPts[1]);
    // botPts 中 x 小的是左下(BL)，大的是右下(BR)
    if (botPts[0].x > botPts[1].x) std::swap(botPts[0], botPts[1]);

    pts[0] = topPts[0]; // TL
    pts[1] = topPts[1]; // TR
    pts[2] = botPts[1]; // BR (注意：你原代码 P3 是右下)
    pts[3] = botPts[0]; // BL
}

/**
 * @brief 根据 m_calibPoints 刷新九点标定坐标显示
 * @details
 *   - P1~P9 显示为：P1: (x, y)
 *   - 未采集的点显示为：P1: 未选
 */
void MainWindow::updateCalibLabels()
{
    auto setLabel = [](QLabel *lbl, int idx, const QVector<cv::Point2f> &pts)
    {
        if (!lbl) return;
        if (idx < pts.size())
        {
            lbl->setText(
                QString("P%1: (%2, %3)")
                    .arg(idx + 1)
                    .arg(pts[idx].x, 0, 'f', 1)
                    .arg(pts[idx].y, 0, 'f', 1));
        }
        else
        {
            lbl->setText(
                QString("P%1: 未选").arg(idx + 1));
        }
    };

    setLabel(ui->labelCalibP1, 0, m_calibPoints);
    setLabel(ui->labelCalibP2, 1, m_calibPoints);
    setLabel(ui->labelCalibP3, 2, m_calibPoints);
    setLabel(ui->labelCalibP4, 3, m_calibPoints);
    setLabel(ui->labelCalibP5, 4, m_calibPoints);
    setLabel(ui->labelCalibP6, 5, m_calibPoints);
    setLabel(ui->labelCalibP7, 6, m_calibPoints);
    setLabel(ui->labelCalibP8, 7, m_calibPoints);
    setLabel(ui->labelCalibP9, 8, m_calibPoints);
}

/**
 * @brief 将一张 QImage 注入为“当前帧”并在显示窗口中显示
 * @details
     1）把图像统一转成 8bit 灰度，写入 m_frameBuffer / m_frameInfo，
     2）同步一份到保存图片用的 m_pSaveImageBuf / m_stImageInfo，
     3）调用 onNewFrame() 更新 DisplayWidget。
 en: Use a QImage as the current frame:
     1) Convert to 8-bit grayscale and write into m_frameBuffer / m_frameInfo,
     2) Sync buffer for SaveImage (m_pSaveImageBuf / m_stImageInfo),
     3) Call onNewFrame() to update DisplayWidget.
 */

void MainWindow::loadImageAsCurrentFrame(const QImage &srcImg)
{
    if (srcImg.isNull())
        return;

    // 统一转成 8bit 灰度，后续算法（取灰度、九点、找圆等）都能用
    QImage grayImg = srcImg.convertToFormat(QImage::Format_Grayscale8);

    int w   = grayImg.width();
    int h   = grayImg.height();
    int len = w * h;

    // 1) 写入回调用的帧缓冲（m_frameBuffer + m_frameInfo）
    {
        QMutexLocker locker(&m_frameMutex);

        m_frameBuffer.resize(len);
        memcpy(m_frameBuffer.data(),
               grayImg.constBits(),
               len);

        memset(&m_frameInfo, 0, sizeof(m_frameInfo));
        m_frameInfo.nWidth      = w;
        m_frameInfo.nHeight     = h;
        m_frameInfo.enPixelType = PixelType_Gvsp_Mono8;
        m_frameInfo.nFrameLen   = len;

        m_hasFrame = true;
    }

    // 2) 顺便更新保存图片用的那套缓存（这样 SaveBmp/Jpg 也能保存这张本地图）
    pthread_mutex_lock(&m_hSaveImageMux);
    if (m_pSaveImageBuf)
    {
        free(m_pSaveImageBuf);
        m_pSaveImageBuf = NULL;
    }
    m_pSaveImageBuf = (unsigned char*)malloc(len);
    if (m_pSaveImageBuf)
    {
        memcpy(m_pSaveImageBuf, grayImg.constBits(), len);
        m_nSaveImageBufSize = len;

        memset(&m_stImageInfo, 0, sizeof(m_stImageInfo));
        m_stImageInfo.nWidth      = w;
        m_stImageInfo.nHeight     = h;
        m_stImageInfo.enPixelType = PixelType_Gvsp_Mono8;
        m_stImageInfo.nFrameLen   = len;
    }
    pthread_mutex_unlock(&m_hSaveImageMux);

    // 3) 调用 onNewFrame 在 DisplayWidget 上显示
    onNewFrame(grayImg);
}

//ROI 框选方案
QRectF MainWindow::currentRoiRect() const
{
    if (!m_hasRoi)
        return QRectF();

    float x1 = m_roiStart.x;
    float y1 = m_roiStart.y;
    float x2 = m_roiEnd.x;
    float y2 = m_roiEnd.y;

    float left   = std::min(x1, x2);
    float top    = std::min(y1, y2);
    float right  = std::max(x1, x2);
    float bottom = std::max(y1, y2);

    return QRectF(left, top, right - left, bottom - top);
}

// clear all
void MainWindow::clearAllOverlays()
{
    // ===== 1. ROI 相关 =====
    m_isSelectingRoi = false;
    m_hasRoi         = false;
    m_roiStart       = cv::Point2f(0.f, 0.f);
    m_roiEnd         = cv::Point2f(0.f, 0.f);

    // ===== 2. 测距相关 =====
    m_isMeasuring       = false;
    m_isDraggingMeasure = false;
    m_measureStart      = cv::Point2f(0.f, 0.f);
    m_measureEnd        = cv::Point2f(0.f, 0.f);

    // ===== 3. 矩形检测叠加 =====
    m_hasBoxOverlay = false;
    m_boxPointsOverlay.clear();   // 存所有矩形 4 点的容器

    m_hasQuad = false;
    m_lastQuad.clear();           // 存最大矩形 4 点的容器

    // ===== 4. 圆检测叠加 =====
    m_hasCircleOverlay    = false;
    m_circleCenterOverlay = cv::Point2f(0.f, 0.f);
    m_circleRadiusOverlay = 0.f;

    // ===== 5. 九点标定点 =====
    m_calibPoints.clear();
    updateCalibLabels();          // 把界面上显示 9 个点坐标的 Label 清掉

    // 如果“清除痕迹”时也想顺便退出标定模式，可以加上这行：
    // m_isCalibrating = false;

    // ===== 6. 用当前原始帧重画一次（不带任何叠加） =====
    emit sigNewFrame(m_displayImage);
}
// 构造 / 析构 //////////////////////////////////////////////////////////////////

/**
 * @brief MainWindow::MainWindow
 * @details
 * 1. 初始化 Qt UI（ui->setupUi）
 * 2. 初始化 QGraphicsView/QGraphicsScene，用于显示相机图像
 * 3. 设置 DisplayWidget 支持拖拽、缩放、鼠标事件
 * 4. 初始化相机相关状态、缓存、互斥锁
 */
MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    memset(&m_stDevList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
    m_pcMyCamera = NULL;
    m_bGrabbing = false;

    // --------- 图像显示相关：QGraphicsView + QGraphicsScene ----------
    m_scene = new QGraphicsScene(this);
    m_pixItem = m_scene->addPixmap(QPixmap());
    ui->DisplayWidget->setScene(m_scene);

    // 回调线程通过信号把 QImage 发送到 UI 线程
    connect(this, &MainWindow::sigNewFrame,
            this, &MainWindow::onNewFrame);

    // QGraphicsView 手型拖拽
    ui->DisplayWidget->setDragMode(QGraphicsView::ScrollHandDrag);

    // 鼠标移动 + 滚轮事件通过 eventFilter 处理
    ui->DisplayWidget->viewport()->setMouseTracking(true);
    ui->DisplayWidget->viewport()->installEventFilter(this);

    // --------- 相机状态变量初始化 ----------
    m_bOpenDevice          = false;
    m_bGrabbing            = false;
    m_bTriggerModeCheck    = false;
    m_bAcquisitionLineRate = false;
    m_bPreampGain          = false;
    m_bHBMode              = false;
    m_hasCircleOverlay = false;
    m_circleRadiusOverlay = 0.0f;

    m_hasBoxOverlay = false;
    m_boxPointsOverlay.clear();

    // 保存图片相关缓存
    m_nSaveImageBufSize    = 0;
    m_pSaveImageBuf        = NULL;

    // 当前帧缓存（供 OpenCV 算法用）
    m_hasFrame             = false;
    m_frameBuffer.clear();
    // m_saveDir 默认空，第一次保存时弹出路径选择框

    // 九点标定状态初始化
    m_isCalibrating = false;
    m_calibPoints.clear();
    updateCalibLabels();   // 初始把 P1~P9 设为“未选”

    //实时显示 FPS + 平均灰度初始化
    m_fpsTimer.start();
    m_frameCountForFps = 0;
    m_currentFps = 0.0;

    // 保存图片互斥锁（与图像回调共享）
    pthread_mutex_init(&m_hSaveImageMux, NULL);
}

MainWindow::~MainWindow()
{
    if (m_pcMyCamera)
    {
        m_pcMyCamera->Close();
        delete m_pcMyCamera;
        m_pcMyCamera = NULL;
    }

    delete ui;
}

// UI 使能控制 //////////////////////////////////////////////////////////////////

/**
 * @brief MainWindow::EnableControls
 * @param bIsCameraReady  当前是否存在可用相机（来自枚举结果）
 * @details
 *   根据 m_bOpenDevice / m_bGrabbing 等状态统一控制所有按钮的使能，
 *   避免无效操作（例如未打开设备就开始采集）。
 */
void MainWindow::EnableControls(bool bIsCameraReady)
{
    ui->OpenButton->setEnabled(m_bOpenDevice ? false : (bIsCameraReady ? true : false));
    ui->CloseButton->setEnabled((m_bOpenDevice && bIsCameraReady) ? true : false);
    ui->StartGrabbingButton->setEnabled((m_bGrabbing && bIsCameraReady) ? false : (m_bOpenDevice ? true : false));
    ui->StopGrabbingButton->setEnabled(m_bGrabbing ? true : false);
    ui->SoftwareOnceButton->setEnabled((m_bGrabbing && m_bTriggerModeCheck) ? true : false);
    ui->SaveBmpButton->setEnabled(m_bGrabbing ? true : false);
    ui->SaveTiffButton->setEnabled(m_bGrabbing ? true : false);
    ui->SavePngButton->setEnabled(m_bGrabbing ? true : false);
    ui->SaveJpgButton->setEnabled(m_bGrabbing ? true : false);
    ui->ExposureTimeLineEdit->setEnabled(m_bOpenDevice ? true : false);
    ui->PreampGainLineEdit->setEnabled(m_bOpenDevice ? true : false);
    ui->AcquisitionLineRateLineEdit->setEnabled((m_bOpenDevice && m_bAcquisitionLineRate) ? true : false);
    ui->ResultingLineRateLineEdit->setEnabled(m_bOpenDevice ? true : false);
    ui->SelchangeTriggerselCombo->setEnabled(m_bOpenDevice ? true : false);
    ui->SelchangeTriggerswitchCombo->setEnabled(m_bOpenDevice ? true : false);
    ui->GetParameterButton->setEnabled(m_bOpenDevice ? true : false);
    ui->SetParameterButton->setEnabled(m_bOpenDevice ? true : false);
    ui->SelchangeTriggersourceCombo->setEnabled(m_bOpenDevice ? true : false);
    ui->SelchangePixelformatCombo->setEnabled((m_bOpenDevice && !m_bGrabbing) ? true : false);
    ui->SelchangeImageCompressionModeCombo->setEnabled((m_bOpenDevice && m_bHBMode && !m_bGrabbing)? true : false);
    ui->SelchangePreampgainCombo->setEnabled((m_bOpenDevice && m_bPreampGain) ? true : false);
    ui->AcquisitionLineRateEnableCheckBox->setEnabled((m_bOpenDevice && m_bAcquisitionLineRate)? true : false);
    ui->ResultingLineRateLineEdit->setEnabled(false);

    if (!m_bOpenDevice)
    {
        // 关闭设备后清空一些数值显示
        ui->AcquisitionLineRateEnableCheckBox->setChecked(false);
        ui->ExposureTimeLineEdit->setText(QString::number(0, 10));
        ui->PreampGainLineEdit->setText(QString::number(0, 10));
        ui->AcquisitionLineRateLineEdit->setText(QString::number(0, 10));
        ui->ResultingLineRateLineEdit->setText(QString::number(0, 10));
    }
}

// 相机参数获取封装 //////////////////////////////////////////////////////////////

/**
 * @brief MainWindow::GetTriggerSelector
 * @return MV_OK / error code
 * @details
 *   读取 TriggerSelector 支持列表与当前值，
 *   同步到 UI 的触发选择下拉框。
 */
int MainWindow::GetTriggerSelector()
{
    MVCC_ENUMVALUE stEnumTriggerSelectorValue = { 0 };
    MVCC_ENUMENTRY stEnumTriggerSelectorEntry = { 0 };

    int nRet = m_pcMyCamera->GetEnumValue("TriggerSelector", &stEnumTriggerSelectorValue);
    if (MV_OK != nRet)
    {
        return nRet;
    }

    ui->SelchangeTriggerselCombo->clear();
    for (int i = 0; i < stEnumTriggerSelectorValue.nSupportedNum; i++)
    {
        memset(&stEnumTriggerSelectorEntry, 0, sizeof(stEnumTriggerSelectorEntry));
        stEnumTriggerSelectorEntry.nValue = stEnumTriggerSelectorValue.nSupportValue[i];
        m_pcMyCamera->GetEnumEntrySymbolic("TriggerSelector", &stEnumTriggerSelectorEntry);

        ui->SelchangeTriggerselCombo->addItem((QString)stEnumTriggerSelectorEntry.chSymbolic);
    }

    for (int i = 0; i < stEnumTriggerSelectorValue.nSupportedNum; i++)
    {
        if (stEnumTriggerSelectorValue.nCurValue == stEnumTriggerSelectorValue.nSupportValue[i])
        {
            m_nTriggerSelector = i;
            ui->SelchangeTriggerselCombo->setCurrentIndex(m_nTriggerSelector);
        }
    }

    return MV_OK;
}

/**
 * @brief MainWindow::GetTriggerMode
 * @details
 *   获取 TriggerMode 支持值及当前值，
 *   同步到触发模式下拉框。
 */
int MainWindow::GetTriggerMode()
{
    MVCC_ENUMVALUE stEnumTriggerModeValue = { 0 };
    MVCC_ENUMENTRY stEnumTriggerModeEntry = { 0 };

    int nRet = m_pcMyCamera->GetEnumValue("TriggerMode", &stEnumTriggerModeValue);
    if (MV_OK != nRet)
    {
        return nRet;
    }

    ui->SelchangeTriggerswitchCombo->clear();
    for (int i = 0; i < stEnumTriggerModeValue.nSupportedNum; i++)
    {
        memset(&stEnumTriggerModeEntry, 0, sizeof(stEnumTriggerModeEntry));
        stEnumTriggerModeEntry.nValue = stEnumTriggerModeValue.nSupportValue[i];
        m_pcMyCamera->GetEnumEntrySymbolic("TriggerMode", &stEnumTriggerModeEntry);
        ui->SelchangeTriggerswitchCombo->addItem((QString)stEnumTriggerModeEntry.chSymbolic);
    }

    for (int i = 0; i < stEnumTriggerModeValue.nSupportedNum; i++)
    {
        if (stEnumTriggerModeValue.nCurValue == stEnumTriggerModeValue.nSupportValue[i])
        {
            m_nTriggerMode = i;
            ui->SelchangeTriggerselCombo->setCurrentIndex(m_nTriggerMode);
        }
    }

    return MV_OK;
}

/**
 * @brief MainWindow::GetTriggerSource
 * @details
 *   获取 TriggerSource 支持列表与当前值，
 *   维护 m_mapTriggerSource 映射，并刷新 UI 下拉框。
 *   同时根据“触发选择+触发模式+触发源”判断是否允许软件触发按钮。
 */
int MainWindow::GetTriggerSource()
{
    MVCC_ENUMVALUE stEnumTriggerSourceValue = { 0 };
    MVCC_ENUMENTRY stEnumTriggerSourceEntry = { 0 };

    int nRet = m_pcMyCamera->GetEnumValue("TriggerSource", &stEnumTriggerSourceValue);
    if (MV_OK != nRet)
    {
        return nRet;
    }

    ui->SelchangeTriggersourceCombo->clear();
    m_mapTriggerSource.clear();

    for (int i = 0; i < stEnumTriggerSourceValue.nSupportedNum; i++)
    {
        memset(&stEnumTriggerSourceEntry, 0, sizeof(stEnumTriggerSourceEntry));
        stEnumTriggerSourceEntry.nValue = stEnumTriggerSourceValue.nSupportValue[i];
        m_pcMyCamera->GetEnumEntrySymbolic("TriggerSource", &stEnumTriggerSourceEntry);

        ui->SelchangeTriggersourceCombo->addItem((QString)stEnumTriggerSourceEntry.chSymbolic);
        m_mapTriggerSource.insert((QString)stEnumTriggerSourceEntry.chSymbolic, stEnumTriggerSourceEntry.nValue);
    }

    for (int i = 0; i < stEnumTriggerSourceValue.nSupportedNum; i++)
    {
        if (stEnumTriggerSourceValue.nCurValue == stEnumTriggerSourceValue.nSupportValue[i])
        {
            m_nTriggerSource = i;
            ui->SelchangeTriggersourceCombo->setCurrentIndex(m_nTriggerSource);
        }
    }

    QString strTriggerSource  = ui->SelchangeTriggersourceCombo->currentText();
    QString strTriggerSelector = ui->SelchangeTriggerselCombo->currentText();
    QString strTriggerMode    = ui->SelchangeTriggerswitchCombo->currentText();
    if (STR_FRAMEBURSTSTART == strTriggerSelector &&
        "On" == strTriggerMode &&
        STR_SOFTWARE == strTriggerSource)
    {
        m_bTriggerModeCheck = true;
    }

    EnableControls(true);
    return MV_OK;
}

/**
 * @brief MainWindow::GetExposureTime
 * @details 读取当前曝光时间并显示到编辑框。
 */
int MainWindow::GetExposureTime()
{
    MVCC_FLOATVALUE stFloatValue = {0};

    int nRet = m_pcMyCamera->GetFloatValue("ExposureTime", &stFloatValue);
    if (MV_OK != nRet)
    {
        return nRet;
    }

    ui->ExposureTimeLineEdit->setText(QString::number(stFloatValue.fCurValue,'f',4));
    return MV_OK;
}

/**
 * @brief MainWindow::GetDigitalShiftGain
 * @details 读取 DigitalShift（数字增益）并显示。
 */
int MainWindow::GetDigitalShiftGain()
{
    MVCC_FLOATVALUE stFloatValue = {0};

    int nRet = m_pcMyCamera->GetFloatValue("DigitalShift", &stFloatValue);
    if (MV_OK != nRet)
    {
        return nRet;
    }

    ui->PreampGainLineEdit->setText(QString::number(stFloatValue.fCurValue,'f',4));
    return MV_OK;
}

/**
 * @brief MainWindow::GetPreampGain
 * @details
 *   获取 PreampGain 支持列表与当前值，同步到 UI 下拉框，
 *   并更新 m_mapPreampGain。
 */
int MainWindow::GetPreampGain()
{
    MVCC_ENUMVALUE stEnumPreampGainValue = { 0 };
    MVCC_ENUMENTRY stEnumPreampGainEntry = { 0 };

    int nRet = m_pcMyCamera->GetEnumValue("PreampGain", &stEnumPreampGainValue);
    if (MV_OK != nRet)
    {
        return nRet;
    }

    ui->SelchangePreampgainCombo->clear();
    m_mapPreampGain.clear();
    for (int i = 0; i < stEnumPreampGainValue.nSupportedNum; i++)
    {
        memset(&stEnumPreampGainEntry, 0, sizeof(stEnumPreampGainEntry));
        stEnumPreampGainEntry.nValue = stEnumPreampGainValue.nSupportValue[i];
        m_pcMyCamera->GetEnumEntrySymbolic("PreampGain", &stEnumPreampGainEntry);

        ui->SelchangePreampgainCombo->addItem((QString)stEnumPreampGainEntry.chSymbolic);
        m_mapPreampGain.insert((QString)stEnumPreampGainEntry.chSymbolic, stEnumPreampGainEntry.nValue);
    }

    for (int i = 0; i < stEnumPreampGainValue.nSupportedNum; i++)
    {
        if (stEnumPreampGainValue.nCurValue == stEnumPreampGainValue.nSupportValue[i])
        {
            m_nPreampGain = i;
            ui->SelchangePreampgainCombo->setCurrentIndex(m_nPreampGain);
        }
    }

    m_bPreampGain = true;
    return MV_OK;
}

/**
 * @brief MainWindow::GetAcquisitionLineRateEnable
 * @details 读取行频使能状态，并同步复选框。
 */
int MainWindow::GetAcquisitionLineRateEnable()
{
    bool bAcquisitionLineRateEnable = false;
    int nRet = m_pcMyCamera->GetBoolValue("AcquisitionLineRateEnable", &bAcquisitionLineRateEnable);
    if (MV_OK != nRet)
    {
        return nRet;
    }

    ui->AcquisitionLineRateEnableCheckBox->setChecked(bAcquisitionLineRateEnable);
    return MV_OK;
}

/**
 * @brief MainWindow::GetAcquisitionLineRate
 * @details 读取行频数值并显示。
 */
int MainWindow::GetAcquisitionLineRate()
{
    MVCC_INTVALUE_EX stIntValue = { 0 };

    int nRet = m_pcMyCamera->GetIntValue("AcquisitionLineRate", &stIntValue);
    if (MV_OK != nRet)
    {
        return nRet;
    }

    ui->AcquisitionLineRateLineEdit->setText(QString::number(stIntValue.nCurValue,10));
    m_bAcquisitionLineRate = true;

    return MV_OK;
}

/**
 * @brief MainWindow::GetResultingLineRate
 * @details 读取实际行频（ResultingLineRate）并显示。
 */
int MainWindow::GetResultingLineRate()
{
    MVCC_INTVALUE_EX stIntValue = { 0 };

    int nRet = m_pcMyCamera->GetIntValue("ResultingLineRate", &stIntValue);
    if (MV_OK != nRet)
    {
        return nRet;
    }

    ui->ResultingLineRateLineEdit->setText(QString::number(stIntValue.nCurValue,10));
    return MV_OK;
}

/**
 * @brief MainWindow::GetPixelFormat
 * @details
 *   获取像素格式支持列表+当前值，
 *   更新像素格式下拉框和 m_mapPixelFormat。
 */
int MainWindow::GetPixelFormat()
{
    m_mapPixelFormat.clear();
    MVCC_ENUMVALUE stEnumPixelFormatValue = { 0 };
    MVCC_ENUMENTRY stEnumPixelFormatEntry = { 0 };

    int nRet = m_pcMyCamera->GetEnumValue("PixelFormat", &stEnumPixelFormatValue);
    if (MV_OK != nRet)
    {
        return nRet;
    }

    ui->SelchangePixelformatCombo->clear();
    for (int i = 0; i < stEnumPixelFormatValue.nSupportedNum; i++)
    {
        memset(&stEnumPixelFormatEntry, 0, sizeof(stEnumPixelFormatEntry));
        stEnumPixelFormatEntry.nValue = stEnumPixelFormatValue.nSupportValue[i];
        m_pcMyCamera->GetEnumEntrySymbolic("PixelFormat", &stEnumPixelFormatEntry);

        ui->SelchangePixelformatCombo->addItem((QString)stEnumPixelFormatEntry.chSymbolic);
        m_mapPixelFormat.insert((QString)stEnumPixelFormatEntry.chSymbolic, stEnumPixelFormatEntry.nValue);
    }

    for (int i = 0; i < stEnumPixelFormatValue.nSupportedNum; i++)
    {
        if (stEnumPixelFormatValue.nCurValue == stEnumPixelFormatValue.nSupportValue[i])
        {
            m_nPixelFormat = i;
            ui->SelchangePixelformatCombo->setCurrentIndex(m_nPixelFormat);
        }
    }

    return MV_OK;
}

/**
 * @brief MainWindow::GetImageCompressionMode
 * @details
 *   获取图像压缩模式支持列表+当前值，
 *   更新压缩模式下拉框。
 */
int MainWindow::GetImageCompressionMode()
{
    MVCC_ENUMVALUE stEnumImageCompressionModeValue = { 0 };
    MVCC_ENUMENTRY stEnumImageCompressionModeEntry = { 0 };

    int nRet = m_pcMyCamera->GetEnumValue("ImageCompressionMode", &stEnumImageCompressionModeValue);
    if (MV_OK != nRet)
    {
        return nRet;
    }

    ui->SelchangeImageCompressionModeCombo->clear();
    for (int i = 0; i < stEnumImageCompressionModeValue.nSupportedNum; i++)
    {
        memset(&stEnumImageCompressionModeEntry, 0, sizeof(stEnumImageCompressionModeEntry));
        stEnumImageCompressionModeEntry.nValue = stEnumImageCompressionModeValue.nSupportValue[i];
        m_pcMyCamera->GetEnumEntrySymbolic("ImageCompressionMode", &stEnumImageCompressionModeEntry);

        ui->SelchangeImageCompressionModeCombo->addItem((QString)stEnumImageCompressionModeEntry.chSymbolic);
    }

    for (int i = 0; i < stEnumImageCompressionModeValue.nSupportedNum; i++)
    {
        if (stEnumImageCompressionModeValue.nCurValue == stEnumImageCompressionModeValue.nSupportValue[i])
        {
            m_nImageCompressionMode = i;
            ui->SelchangeImageCompressionModeCombo->setCurrentIndex(m_nImageCompressionMode);
        }
    }

    m_bHBMode = true;
    return MV_OK;
}

// 错误信息 //////////////////////////////////////////////////////////////////////

/**
 * @brief MainWindow::ShowErrorMsg
 * @param csMessage   前缀消息
 * @param nErrorNum   SDK 返回错误码（可为 0）
 * @details
 *   将 SDK 错误码转成可读文本，在 QMessageBox 中弹出。
 */
void MainWindow::ShowErrorMsg(QString csMessage, unsigned int nErrorNum)
{
    QString errorMsg = csMessage;
    if (nErrorNum != 0)
    {
        QString TempMsg;
        TempMsg.sprintf(": Error = %x: ", nErrorNum);
        errorMsg += TempMsg;
    }

    switch(nErrorNum)
    {
    case MV_E_HANDLE:           errorMsg += "Error or invalid handle ";                                         break;
    case MV_E_SUPPORT:          errorMsg += "Not supported function ";                                          break;
    case MV_E_BUFOVER:          errorMsg += "Cache is full ";                                                   break;
    case MV_E_CALLORDER:        errorMsg += "Function calling order error ";                                    break;
    case MV_E_PARAMETER:        errorMsg += "Incorrect parameter ";                                             break;
    case MV_E_RESOURCE:         errorMsg += "Applying resource failed ";                                        break;
    case MV_E_NODATA:           errorMsg += "No data ";                                                         break;
    case MV_E_PRECONDITION:     errorMsg += "Precondition error, or running environment changed ";              break;
    case MV_E_VERSION:          errorMsg += "Version mismatches ";                                              break;
    case MV_E_NOENOUGH_BUF:     errorMsg += "Insufficient memory ";                                             break;
    case MV_E_ABNORMAL_IMAGE:   errorMsg += "Abnormal image, maybe incomplete image because of lost packet ";   break;
    case MV_E_UNKNOW:           errorMsg += "Unknown error ";                                                   break;
    case MV_E_GC_GENERIC:       errorMsg += "General error ";                                                   break;
    case MV_E_GC_ACCESS:        errorMsg += "Node accessing condition error ";                                  break;
    case MV_E_ACCESS_DENIED:    errorMsg += "No permission ";                                                   break;
    case MV_E_BUSY:             errorMsg += "Device is busy, or network disconnected ";                         break;
    case MV_E_NETER:            errorMsg += "Network error ";                                                   break;
    }

    QMessageBox::information(NULL, "PROMPT", errorMsg);
}

// 设备枚举 / 打开 / 关闭 / 采集 ///////////////////////////////////////////////////

/**
 * @brief MainWindow::on_EnumButton_clicked
 * @details
 *   “查找设备”按钮槽函数：
 *   1. 调用海康 SDK 枚举当前网络/总线上的相机
 *   2. 将结果填充到下拉框 EnumCombo 中
 *   3. 更新按钮使能状态
 */
void MainWindow::on_EnumButton_clicked()
{
    ui->EnumCombo->clear();
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("GBK"));
    ui->EnumCombo->setStyle(QStyleFactory::create("Windows"));

    memset(&m_stDevList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
    int nRet = CMvCamera::EnumDevices(
                MV_GIGE_DEVICE |
                MV_USB_DEVICE |
                MV_GENTL_CAMERALINK_DEVICE |
                MV_GENTL_CXP_DEVICE |
                MV_GENTL_XOF_DEVICE,
                &m_stDevList);
    if (MV_OK != nRet)
    {
        return;
    }

    // 枚举结果填充到下拉框
    for (unsigned int i = 0; i < m_stDevList.nDeviceNum; i++)
    {
        MV_CC_DEVICE_INFO* pDeviceInfo = m_stDevList.pDeviceInfo[i];
        if (NULL == pDeviceInfo)
        {
            continue;
        }
        char strUserName[256] = {0};

        // 下面几段分别处理不同总线类型的字符串显示
        if (pDeviceInfo->nTLayerType == MV_GIGE_DEVICE)
        {
            int nIp1 = ((m_stDevList.pDeviceInfo[i]->SpecialInfo.stGigEInfo.nCurrentIp & 0xff000000) >> 24);
            int nIp2 = ((m_stDevList.pDeviceInfo[i]->SpecialInfo.stGigEInfo.nCurrentIp & 0x00ff0000) >> 16);
            int nIp3 = ((m_stDevList.pDeviceInfo[i]->SpecialInfo.stGigEInfo.nCurrentIp & 0x0000ff00) >> 8);
            int nIp4 = (m_stDevList.pDeviceInfo[i]->SpecialInfo.stGigEInfo.nCurrentIp & 0x000000ff);

            if (strcmp("", (char*)pDeviceInfo->SpecialInfo.stGigEInfo.chUserDefinedName) != 0)
            {
                snprintf(strUserName, 256, "[%d]GigE:   %s (%s) (%d.%d.%d.%d)", i,
                         pDeviceInfo->SpecialInfo.stGigEInfo.chUserDefinedName,
                         pDeviceInfo->SpecialInfo.stGigEInfo.chSerialNumber,
                         nIp1, nIp2, nIp3, nIp4);
            }
            else
            {
                snprintf(strUserName, 256, "[%d]GigE:   %s (%s) (%d.%d.%d.%d)", i,
                         pDeviceInfo->SpecialInfo.stGigEInfo.chModelName,
                         pDeviceInfo->SpecialInfo.stGigEInfo.chSerialNumber,
                         nIp1, nIp2, nIp3, nIp4);
            }
        }
        else if (pDeviceInfo->nTLayerType == MV_USB_DEVICE)
        {
            if (strcmp("", (char*)pDeviceInfo->SpecialInfo.stUsb3VInfo.chUserDefinedName) != 0)
            {
                snprintf(strUserName, 256, "[%d]UsbV3:  %s (%s)", i,
                         pDeviceInfo->SpecialInfo.stUsb3VInfo.chUserDefinedName,
                         pDeviceInfo->SpecialInfo.stUsb3VInfo.chSerialNumber);
            }
            else
            {
                snprintf(strUserName, 256, "[%d]UsbV3:  %s (%s)", i,
                         pDeviceInfo->SpecialInfo.stUsb3VInfo.chModelName,
                         pDeviceInfo->SpecialInfo.stUsb3VInfo.chSerialNumber);
            }
        }
        else if (pDeviceInfo->nTLayerType == MV_GENTL_CAMERALINK_DEVICE)
        {
            if (strcmp("", (char*)pDeviceInfo->SpecialInfo.stCMLInfo.chUserDefinedName) != 0)
            {
                snprintf(strUserName, 256, "[%d]CML:  %s (%s)", i,
                         pDeviceInfo->SpecialInfo.stCMLInfo.chUserDefinedName,
                         pDeviceInfo->SpecialInfo.stCMLInfo.chSerialNumber);
            }
            else
            {
                snprintf(strUserName, 256, "[%d]CML:  %s (%s)", i,
                         pDeviceInfo->SpecialInfo.stCMLInfo.chModelName,
                         pDeviceInfo->SpecialInfo.stCMLInfo.chSerialNumber);
            }
        }
        else if (pDeviceInfo->nTLayerType == MV_GENTL_CXP_DEVICE)
        {
            if (strcmp("", (char*)pDeviceInfo->SpecialInfo.stCXPInfo.chUserDefinedName) != 0)
            {
                snprintf(strUserName, 256, "[%d]CXP:  %s (%s)", i,
                         pDeviceInfo->SpecialInfo.stCXPInfo.chUserDefinedName,
                         pDeviceInfo->SpecialInfo.stCXPInfo.chSerialNumber);
            }
            else
            {
                snprintf(strUserName, 256, "[%d]CXP:  %s (%s)", i,
                         pDeviceInfo->SpecialInfo.stCXPInfo.chModelName,
                         pDeviceInfo->SpecialInfo.stCXPInfo.chSerialNumber);
            }
        }
        else if (pDeviceInfo->nTLayerType == MV_GENTL_XOF_DEVICE)
        {
            if (strcmp("", (char*)pDeviceInfo->SpecialInfo.stXoFInfo.chUserDefinedName) != 0)
            {
                snprintf(strUserName, 256, "[%d]XOF:  %s (%s)", i,
                         pDeviceInfo->SpecialInfo.stXoFInfo.chUserDefinedName,
                         pDeviceInfo->SpecialInfo.stXoFInfo.chSerialNumber);
            }
            else
            {
                snprintf(strUserName, 256, "[%d]XOF:  %s (%s)",i,
                         pDeviceInfo->SpecialInfo.stXoFInfo.chModelName,
                         pDeviceInfo->SpecialInfo.stXoFInfo.chSerialNumber);
            }
        }
        else
        {
            ShowErrorMsg("Unknown device enumerated", 0);
        }
        ui->EnumCombo->addItem(QString::fromLocal8Bit(strUserName));
    }

    if (0 == m_stDevList.nDeviceNum)
    {
        ShowErrorMsg("No device", 0);
        return;
    }
    ui->EnumCombo->setCurrentIndex(0);

    EnableControls(true);
}

/**
 * @brief MainWindow::on_OpenButton_clicked
 * @details
 *   “打开设备”按钮槽函数：
 *   1. 根据当前枚举索引创建并打开相机对象
 *   2. 对 GigE 相机自动优化包大小
 *   3. 同步当前相机参数到 UI
 */
void MainWindow::on_OpenButton_clicked()
{
    int nIndex = ui->EnumCombo->currentIndex();
    if ((nIndex < 0) | (nIndex >= MV_MAX_DEVICE_NUM))
    {
        ShowErrorMsg("Please select device", 0);
        return;
    }

    if (NULL == m_stDevList.pDeviceInfo[nIndex])
    {
        ShowErrorMsg("Device does not exist", 0);
        return;
    }

    if(m_pcMyCamera == NULL)
    {
        m_pcMyCamera = new CMvCamera;
        if (NULL == m_pcMyCamera)
        {
            return;
        }
    }

    int nRet = m_pcMyCamera->Open(m_stDevList.pDeviceInfo[nIndex]);
    if (MV_OK != nRet)
    {
        delete m_pcMyCamera;
        m_pcMyCamera = NULL;
        ShowErrorMsg("Open Fail", nRet);
        return;
    }

    // GigE 相机：自动探测并设置最优包大小
    if (m_stDevList.pDeviceInfo[nIndex]->nTLayerType == MV_GIGE_DEVICE)
    {
        unsigned int nPacketSize = 0;
        nRet = m_pcMyCamera->GetOptimalPacketSize(&nPacketSize);
        if (nRet == MV_OK)
        {
            nRet = m_pcMyCamera->SetIntValue("GevSCPSPacketSize",nPacketSize);
            if(nRet != MV_OK)
            {
                ShowErrorMsg("Warning: Set Packet Size fail!", nRet);
            }
        }
        else
        {
            ShowErrorMsg("Warning: Get Packet Size fail!", nRet);
        }
    }

    m_bOpenDevice = true;
    EnableControls(true);
    ui->ExposureTimeLineEdit->setEnabled(m_bOpenDevice ? true : false);

    // 打开后立即从设备读取一次参数
    on_GetParameterButton_clicked();

    nRet = GetPixelFormat();
    if (nRet != MV_OK)
    {
        ui->SelchangePixelformatCombo->setEnabled(false);
    }

    nRet = GetImageCompressionMode();
    if (nRet != MV_OK)
    {
        ui->SelchangeImageCompressionModeCombo->setEnabled(false);
    }

    EnableControls(true);
}

/**
 * @brief MainWindow::on_GetParameterButton_clicked
 * @details
 *   “参数获取”按钮槽函数：
 *   统一调用各个 GetXXX 函数，从相机读取当前配置并刷新 UI。
 */
void MainWindow::on_GetParameterButton_clicked()
{
    int nRet = GetTriggerSelector();
    if (nRet != MV_OK)
    {
        ui->SelchangeTriggerselCombo->setEnabled(false);
    }

    nRet = GetTriggerMode();
    if (nRet != MV_OK)
    {
        ui->SelchangeTriggerswitchCombo->setEnabled(false);
    }

    nRet = GetTriggerSource();
    if (nRet != MV_OK)
    {
        ui->SelchangeTriggersourceCombo->setEnabled(false);
    }

    nRet = GetExposureTime();
    if (nRet != MV_OK)
    {
        ui->ExposureTimeLineEdit->setEnabled(false);
    }

    nRet = GetDigitalShiftGain();
    if (nRet != MV_OK)
    {
        ui->PreampGainLineEdit->setEnabled(false);
    }

    nRet = GetPreampGain();
    if (nRet != MV_OK)
    {
        ui->SelchangePreampgainCombo->setEnabled(false);
    }

    nRet = GetAcquisitionLineRateEnable();
    if (nRet != MV_OK)
    {
        ui->AcquisitionLineRateEnableCheckBox->setEnabled(false);
    }

    nRet = GetAcquisitionLineRate();
    if (nRet != MV_OK)
    {
        ui->AcquisitionLineRateLineEdit->setEnabled(false);
    }

    nRet = GetResultingLineRate();
    if (nRet != MV_OK)
    {
        ui->ResultingLineRateLineEdit->setEnabled(false);
    }
}

/**
 * @brief MainWindow::ImageCallBack2
 * @details
 *   图像回调静态入口函数，转发到 MainWindow 成员函数
 *   ImageCallBackInner2，pUser 为 this。
 */
void __stdcall MainWindow::ImageCallBack2(MV_FRAME_OUT* pstFrame, void *pUser, bool bAutoFree)
{
    if(pUser)
    {
        MainWindow *pMainWindow = (MainWindow*)pUser;
        pMainWindow->ImageCallBackInner2(pstFrame, bAutoFree);
    }
}

/**
 * @brief MainWindow::ImageCallBackInner2
 * @details
 *   相机采集线程回调：
 *   1. 维护保存图片用的 m_pSaveImageBuf/m_stImageInfo
 *   2. 控制 UI 更新频率（每 N 帧更新一次）
 *   3. 将当前帧拷贝到 m_frameBuffer，供 OpenCV 算法/灰度查询使用
 *   4. 构造 QImage，通过 sigNewFrame 发送给 UI 线程显示
 *   5. 若为非自动释放模式，则手动释放缓存
 *
 *   线程安全：
 *   - 使用 m_hSaveImageMux 保护保存图片缓冲区
 *   - 使用 m_frameMutex 保护 m_frameBuffer/m_frameInfo/m_hasFrame
 */
void MainWindow::ImageCallBackInner2(MV_FRAME_OUT* pstFrame, bool bAutoFree)
{
    // 1) 保存图片相关缓存（与 SaveImage 共享）
    pthread_mutex_lock(&m_hSaveImageMux);
    if (NULL == m_pSaveImageBuf || pstFrame->stFrameInfo.nFrameLen > m_nSaveImageBufSize)
    {
        if (m_pSaveImageBuf)
        {
            free(m_pSaveImageBuf);
            m_pSaveImageBuf = NULL;
        }

        m_pSaveImageBuf = (unsigned char *)malloc(sizeof(unsigned char) * pstFrame->stFrameInfo.nFrameLen);
        if (m_pSaveImageBuf == NULL)
        {
            pthread_mutex_unlock(&m_hSaveImageMux);
            if (false == bAutoFree)
            {
                m_pcMyCamera->FreeImageBuffer(pstFrame);
            }
            return;
        }
        m_nSaveImageBufSize = pstFrame->stFrameInfo.nFrameLen;
    }

    memcpy(m_pSaveImageBuf, pstFrame->pBufAddr, pstFrame->stFrameInfo.nFrameLen);
    memcpy(&m_stImageInfo, &(pstFrame->stFrameInfo), sizeof(MV_FRAME_OUT_INFO_EX));
    pthread_mutex_unlock(&m_hSaveImageMux);

    // 2) 控制 UI 更新频率，避免高帧率时 UI 卡死
    static int s_frameCount = 0;
    s_frameCount++;
    const int N = 5;                 // 每 N 帧更新一次，可根据需要调整
    bool doUpdate = (s_frameCount % N == 0);

    if (doUpdate)
    {
        // 2.1 缓存当前帧（供“查灰度”“找圆”“找盒子”等功能使用）
        {
            QMutexLocker locker(&m_frameMutex);

            m_frameBuffer.resize(pstFrame->stFrameInfo.nFrameLen);
            memcpy(m_frameBuffer.data(),
                   pstFrame->pBufAddr,
                   pstFrame->stFrameInfo.nFrameLen);

            m_frameInfo = pstFrame->stFrameInfo;
            m_hasFrame  = true;
        }

        // 2.2 构建 QImage，发给 UI 线程刷新 QGraphicsView
        int width  = pstFrame->stFrameInfo.nWidth;
        int height = pstFrame->stFrameInfo.nHeight;
        MvGvspPixelType pixelType = pstFrame->stFrameInfo.enPixelType;

        QImage qimg;

        if (pixelType == PixelType_Gvsp_Mono8)
        {
            qimg = QImage(pstFrame->pBufAddr,
                          width, height,
                          pstFrame->stFrameInfo.nWidth,
                          QImage::Format_Grayscale8).copy();
        }
        else if (pixelType == PixelType_Gvsp_BGR8_Packed)
        {
            QImage tmp(pstFrame->pBufAddr,
                       width, height,
                       pstFrame->stFrameInfo.nWidth * 3,
                       QImage::Format_BGR888);
            qimg = tmp.copy();
        }
        else if (pixelType == PixelType_Gvsp_RGB8_Packed)
        {
            QImage tmp(pstFrame->pBufAddr,
                       width, height,
                       pstFrame->stFrameInfo.nWidth * 3,
                       QImage::Format_RGB888);
            qimg = tmp.copy();
        }

        if (!qimg.isNull())
        {
            emit sigNewFrame(qimg);
        }
    }

    // 3) 非自动释放模式：SDK 要求手动释放图像缓存
    if (false == bAutoFree)
    {
        m_pcMyCamera->FreeImageBuffer(pstFrame);
    }
}

/**
 * @brief MainWindow::on_StartGrabbingButton_clicked
 * @details
 *   “开始采集”按钮槽：
 *   1. 注册图像回调
 *   2. 调用 StartGrabbing 启动连续采集
 *   3. 根据触发配置决定软件触发按钮是否可用
 */
void MainWindow::on_StartGrabbingButton_clicked()
{
    if (false == m_bOpenDevice || true == m_bGrabbing || NULL == m_pcMyCamera)
    {
        return;
    }
    m_pcMyCamera->RegisterImageCallBack2(ImageCallBack2, this, true);

    int nRet = m_pcMyCamera->StartGrabbing();
    if (MV_OK != nRet)
    {
        ShowErrorMsg("Start grabbing fail", nRet);
        return;
    }
    m_bGrabbing = true;
    EnableControls(true);

    QString QstrTriggerSource = ui->SelchangeTriggersourceCombo->currentText();

    if (STR_SOFTWARE == QstrTriggerSource && m_bTriggerModeCheck == true)
    {
        ui->SoftwareOnceButton->setEnabled(true);
    }
    else
    {
        ui->SoftwareOnceButton->setEnabled(false);
    }
}

/**
 * @brief MainWindow::CloseDevice
 * @details
 *   统一的“关闭设备”流程：
 *   1. 如在采集中则先 StopGrabbing
 *   2. 关闭并释放相机对象
 *   3. 释放保存图片缓冲区
 *   4. 重置状态标志
 */
int MainWindow::CloseDevice()
{
    if(true == m_bGrabbing)
    {
        int nRet = m_pcMyCamera->StopGrabbing();
        if (MV_OK != nRet)
        {
            ShowErrorMsg("Stop grabbing fail", nRet);
            return nRet;
        }
        m_bGrabbing = false;
    }

    if (m_pcMyCamera)
    {
        m_pcMyCamera->Close();
        delete m_pcMyCamera;
        m_pcMyCamera = NULL;
    }

    m_bGrabbing   = false;
    m_bOpenDevice = false;

    if (m_pSaveImageBuf)
    {
        free(m_pSaveImageBuf);
        m_pSaveImageBuf = NULL;
    }
    m_nSaveImageBufSize = 0;

    return MV_OK;
}

/**
 * @brief MainWindow::on_CloseButton_clicked
 * @details UI “关闭设备” 按钮槽。
 */
void MainWindow::on_CloseButton_clicked()
{
    CloseDevice();
    m_bTriggerModeCheck    = false;
    m_bAcquisitionLineRate = false;
    m_bPreampGain          = false;
    m_bHBMode              = false;
    EnableControls(true);
}

/**
 * @brief MainWindow::on_StopGrabbingButton_clicked
 * @details “停止采集”按钮槽，停止采集后刷新参数显示。
 */
void MainWindow::on_StopGrabbingButton_clicked()
{
    if (false == m_bOpenDevice || false == m_bGrabbing || NULL == m_pcMyCamera)
    {
        return;
    }
    int nRet = m_pcMyCamera->StopGrabbing();
    if (MV_OK != nRet)
    {
        ShowErrorMsg("Stop grabbing fail", nRet);
        return;
    }
    m_bGrabbing = false;
    EnableControls(true);
    on_GetParameterButton_clicked();
}

// 参数设置 //////////////////////////////////////////////////////////////////////

/**
 * @brief MainWindow::SetExposureTime
 * @details
 *   关闭自动曝光后设置手动曝光时间，
 *   值来源于 m_dExposureEdit（UI 编辑框）。
 */
int MainWindow::SetExposureTime()
{
    m_pcMyCamera->SetEnumValue("ExposureAuto", MV_EXPOSURE_AUTO_MODE_OFF);
    return m_pcMyCamera->SetFloatValue("ExposureTime", (float)m_dExposureEdit);
}

/**
 * @brief MainWindow::SetDigitalShiftGain
 * @details
 *   打开 DigitalShiftEnable 并设置数字增益。
 */
int MainWindow::SetDigitalShiftGain()
{
    m_pcMyCamera->SetBoolValue("DigitalShiftEnable", true);
    return m_pcMyCamera->SetFloatValue("DigitalShift", (float)m_dDigitalShiftGainEdit);
}

/**
 * @brief MainWindow::SetAcquisitionLineRate
 * @details 设置行频，值来源于 m_nAcquisitionLineRateEdit。
 */
int MainWindow::SetAcquisitionLineRate()
{
    return m_pcMyCamera->SetIntValue("AcquisitionLineRate", (int)m_nAcquisitionLineRateEdit);
}

/**
 * @brief MainWindow::on_SetParameterButton_clicked
 * @details
 *   “参数设置”按钮槽：
 *   批量调用 SetExposureTime / SetDigitalShiftGain / SetAcquisitionLineRate，
 *   统一提示设置成功/失败。
 */
void MainWindow::on_SetParameterButton_clicked()
{
    bool bIsSetSucceed = true;
    int nRet = SetExposureTime();
    if (nRet != MV_OK)
    {
        bIsSetSucceed = false;
        ShowErrorMsg("Set Exposure Time Fail", nRet);
    }
    nRet = SetDigitalShiftGain();
    if (nRet != MV_OK)
    {
        bIsSetSucceed = false;
        ShowErrorMsg("Set Digital Shift Fail", nRet);
    }

    if (true == m_bAcquisitionLineRate)
    {
        nRet = SetAcquisitionLineRate();
        if (nRet != MV_OK)
        {
            bIsSetSucceed = false;
            ShowErrorMsg("Set Acquisition Line Rate Fail", nRet);
        }
    }

    if (true == bIsSetSucceed)
    {
        ShowErrorMsg("Set Parameter Succeed", nRet);
    }
}


/**
 * @brief 槽函数：触发选择下拉框内容变化
 * @param arg1  当前选中的 TriggerSelector 名称
 * @details
 *  - 根据用户选择设置相机的 TriggerSelector（FrameBurstStart / LineStart）
 *  - 更新软件触发使能检查标志 m_bTriggerModeCheck
 *  - 重新读取 TriggerSource 以保持 UI 状态与设备一致
 */
void MainWindow::on_SelchangeTriggerselCombo_currentTextChanged(const QString &arg1)
{
    if (STR_FRAMEBURSTSTART == arg1)
    {
        int nRet = m_pcMyCamera->SetEnumValue("TriggerSelector", FRAMEBURSTSTART);
        if (MV_OK != nRet)
        {
            ShowErrorMsg("Set TriggerSelector FrameBurstStart fail", nRet);
            return;
        }

        QString QStrTriggerMode = ui->SelchangeTriggerswitchCombo->currentText();

        QString QStrTriggerSource = ui->SelchangeTriggersourceCombo->currentText();

        if ("On" == QStrTriggerMode && STR_SOFTWARE == QStrTriggerSource)
        {
            m_bTriggerModeCheck = true;
        }
    }
    else if (arg1 == "LineStart")
    {
        int nRet = m_pcMyCamera->SetEnumValue("TriggerSelector", LINESTART);
        if (MV_OK != nRet)
        {
            ShowErrorMsg("Set TriggerSelector LineStart fail", nRet);
            return;
        }
        m_bTriggerModeCheck = false;
    }

    int nRet = GetTriggerSource();
    if (nRet != MV_OK)
    {
        ShowErrorMsg("Get Trigger Source Fail", nRet);
        return;
    }

    EnableControls(true);
}

/**
 * @brief 槽函数：触发模式下拉框内容变化
 * @param arg1 当前选中的 TriggerMode 文本（"On"/"Off"）
 * @details
 *  - 将相机 TriggerMode 置为 On 或 Off
 *  - 结合当前 TriggerSelector、TriggerSource 决定 m_bTriggerModeCheck
 *  - 更新按钮使能状态（影响软件触发按钮）
 */
void MainWindow::on_SelchangeTriggerswitchCombo_currentTextChanged(const QString &arg1)
{
    if ("On" == arg1)
    {
        int nRet = m_pcMyCamera->SetEnumValue("TriggerMode", MV_TRIGGER_MODE_ON);
        if (MV_OK != nRet)
        {
            ShowErrorMsg("Set Trigger Mode fail", nRet);
            return;
        }

        QString QStrTriggerSelector = ui->SelchangeTriggerselCombo->currentText();

        QString QStrTriggerSource = ui->SelchangeTriggersourceCombo->currentText();
        if (STR_FRAMEBURSTSTART == QStrTriggerSelector && STR_SOFTWARE == QStrTriggerSource)
        {
            m_bTriggerModeCheck = true;
        }
    }
    else if ("Off" == arg1)
    {
        int nRet = m_pcMyCamera->SetEnumValue("TriggerMode", MV_TRIGGER_MODE_OFF);
        if (MV_OK != nRet)
        {
            ShowErrorMsg("Set Trigger Mode fail", nRet);
            return;
        }
        m_bTriggerModeCheck = false;
    }

    EnableControls(true);
}

/**
 * @brief 槽函数：触发源下拉框内容变化
 * @param arg1 当前选中的 TriggerSource 文本
 * @details
 *  - 将 TriggerSource 设置到相机（软触发/Line0/Line1 等）
 *  - 根据 TriggerSelector + TriggerMode + TriggerSource 计算 m_bTriggerModeCheck
 *  - 当选择为 STR_SOFTWARE 时，优先设置为软件触发源
 */
void MainWindow::on_SelchangeTriggersourceCombo_currentTextChanged(const QString &arg1)
{
    m_bTriggerModeCheck = false;

    if (STR_SOFTWARE == arg1)
    {
        int nRet = m_pcMyCamera->SetEnumValue("TriggerSource", MV_TRIGGER_SOURCE_SOFTWARE);
        if (MV_OK != nRet)
        {
            ShowErrorMsg("Set Trigger Source fail", nRet);
            return;
        }

        QString QStrTriggerSelector = ui->SelchangeTriggerselCombo->currentText();

        QString QStrTriggerMode = ui->SelchangeTriggerswitchCombo->currentText();
        if (STR_FRAMEBURSTSTART == QStrTriggerSelector && "On" == QStrTriggerMode)
        {
            m_bTriggerModeCheck = true;
        }
    }


    for (QMap<QString, int>::iterator it = m_mapTriggerSource.begin(); it != m_mapTriggerSource.end(); it++)
    {
        if (it.key() == arg1)
        {
            int nRet = m_pcMyCamera->SetEnumValue("TriggerSource", it.value());
            if (MV_OK != nRet)
            {
                ShowErrorMsg("Set TriggerSource fail", nRet);
                return;
            }
            break;
        }
    }

    EnableControls(true);
}

/**
 * @brief 槽函数：软件单次触发按钮
 * @details
 *  - 前置条件：处于采集中、触发模式为软件触发
 *  - 功能：调用 "TriggerSoftware" 命令，让相机出一帧图
 */
void MainWindow::on_SoftwareOnceButton_clicked()
{
    if (true != m_bGrabbing)
    {
        return;
    }

    m_pcMyCamera->CommandExecute("TriggerSoftware");
}

/**
 * @brief 槽函数：像素格式下拉框内容变化
 * @param arg1 当前选中的 PixelFormat 名称
 * @details
 *  - 通过 m_mapPixelFormat 将文本映射到枚举值并设置到相机
 *  - 如处于 HB 压缩模式，重新读取 ImageCompressionMode
 */
void MainWindow::on_SelchangePixelformatCombo_currentTextChanged(const QString &arg1)
{
    for (QMap<QString, int>::iterator it = m_mapPixelFormat.begin(); it != m_mapPixelFormat.end(); it++)
    {
        if (it.key() == arg1)
        {
            int nRet = m_pcMyCamera->SetEnumValue("PixelFormat", it.value());
            if (MV_OK != nRet)
            {
                ShowErrorMsg("Set PixelFormat fail", nRet);
                return;
            }
            break;
        }
    }

    if (true == m_bHBMode)
    {
        int nRet = GetImageCompressionMode();
        if (nRet != MV_OK)
        {
            ShowErrorMsg("Get Image Compression Mode Fail", nRet);
            return;
        }
    }
}

/**
 * @brief 槽函数：图像压缩模式下拉框变化
 * @param arg1 当前选中的压缩模式文本（Off/HB）
 * @details
 *  - 设置相机 "ImageCompressionMode"
 */
void MainWindow::on_SelchangeImageCompressionModeCombo_currentTextChanged(const QString &arg1)
{
    if ("Off" == arg1)
    {
        int nRet = m_pcMyCamera->SetEnumValue("ImageCompressionMode",IMAGE_COMPRESSION_MODE_OFF);
        if (MV_OK != nRet)
        {
            ShowErrorMsg("Set Image Compression Mode fail", nRet);
            return;
        }
    }
    else if ("HB" == arg1)
    {
        int nRet = m_pcMyCamera->SetEnumValue("ImageCompressionMode", IMAGE_COMPRESSION_MODE_HB);
        if (MV_OK != nRet)
        {
            ShowErrorMsg("Set Image Compression Mode fail", nRet);
            return;
        }
    }
}

/**
 * @brief 槽函数：曝光时间编辑框完成编辑
 * @details
 *  - 读取 UI 中曝光时间文本，更新成员 m_dExposureEdit
 *  - 真正写回相机在 on_SetParameterButton_clicked 中完成
 */
void MainWindow::on_ExposureTimeLineEdit_editingFinished()
{
    QString  QStrExposureTime = ui->ExposureTimeLineEdit->text();
    m_dExposureEdit=QStrExposureTime.toFloat();
    //m_pcMyCamera->SetFloatValue("ExposureTime", (float)QStrExposureTime.toDouble());
}

/**
 * @brief 槽函数：模拟增益下拉框变化
 * @param arg1 当前选中的 PreampGain 文本
 * @details
 *  - 通过 m_mapPreampGain 将文本映射到枚举值并设置到相机
 */
void MainWindow::on_SelchangePreampgainCombo_currentTextChanged(const QString &arg1)
{
    for (QMap<QString, int>::iterator it = m_mapPreampGain.begin(); it != m_mapPreampGain.end(); it++)
    {
        if (it.key() == arg1)
        {
            int nRet = m_pcMyCamera->SetEnumValue("PreampGain", it.value());
            if (MV_OK != nRet)
            {
                ShowErrorMsg("Set PreampGain fail", nRet);
                return;
            }
            break;
        }
    }
}

/**
 * @brief 槽函数：数字增益编辑框完成编辑
 * @details
 *  - 读取数字增益文本，更新成员 m_dDigitalShiftGainEdit
 *  - 实际写入相机由 on_SetParameterButton_clicked 统一处理
 */
void MainWindow::on_PreampGainLineEdit_editingFinished()
{
    QString  QStrPreampGain = ui->PreampGainLineEdit->text();
    m_dDigitalShiftGainEdit=QStrPreampGain.toFloat();
}

/**
 * @brief 槽函数：行频编辑框完成编辑
 * @details
 *  - 读取行频文本，更新成员 m_nAcquisitionLineRateEdit
 *  - 实际写入相机由 on_SetParameterButton_clicked 完成
 */
void MainWindow::on_AcquisitionLineRateLineEdit_editingFinished()
{
    QString  QStrAcquisitionLineRate = ui->AcquisitionLineRateLineEdit->text();
    m_nAcquisitionLineRateEdit=QStrAcquisitionLineRate.toInt();

}

/**
 * @brief 槽函数：行频使能复选框点击
 * @param checked 是否勾选
 * @details
 *  - 将 AcquisitionLineRateEnable 设置到相机
 */
void MainWindow::on_AcquisitionLineRateEnableCheckBox_clicked(bool checked)
{
    if (true ==  checked)
    {
        int nRet = m_pcMyCamera->SetBoolValue("AcquisitionLineRateEnable", true);
        if (MV_OK != nRet)
        {
            ShowErrorMsg("Set Acquisition LineRate Enable fail", nRet);
            return;
        }
    }
    else
    {
        int nRet = m_pcMyCamera->SetBoolValue("AcquisitionLineRateEnable", false);
        if (MV_OK != nRet)
        {
            ShowErrorMsg("Set Acquisition LineRate Enable fail", nRet);
            return;
        }
    }
}

//// ch:保存图片 | en:Save Image
//int MainWindow::SaveImage(MV_SAVE_IAMGE_TYPE enSaveImageType)
//{
//    MV_SAVE_IMAGE_TO_FILE_PARAM_EX stSaveFileParam;//MV_SAVE_IMAGE_TO_FILE_PARAM_EX 是海康 SDK 用来 “保存成文件” 的参数结构体
//    memset(&stSaveFileParam, 0, sizeof(MV_SAVE_IMAGE_TO_FILE_PARAM_EX));//data to zero

//    pthread_mutex_lock(&m_hSaveImageMux);/*加锁*/
//    if (m_pSaveImageBuf == NULL || m_stImageInfo.enPixelType == 0)//说明还没收到任何一帧图像或者说明图像信息不完整
//    {
//        pthread_mutex_unlock(&m_hSaveImageMux);/*解锁*/
//        return MV_E_NODATA;//no data
//    }

//    stSaveFileParam.enImageType = enSaveImageType; // ch:需要保存的图像类型 | en:Image format to save
//    stSaveFileParam.enPixelType = m_stImageInfo.enPixelType;  // ch:相机对应的像素格式 | en:Camera pixel type
//    stSaveFileParam.nWidth      = m_stImageInfo.nWidth;         // ch:相机对应的宽 | en:Width
//    stSaveFileParam.nHeight     = m_stImageInfo.nHeight;          // ch:相机对应的高 | en:Height
//    stSaveFileParam.nDataLen    = m_stImageInfo.nFrameLen;
//    stSaveFileParam.pData       = m_pSaveImageBuf;
//    stSaveFileParam.pcImagePath=(char*)malloc(256); //malloc image
//    memset(stSaveFileParam.pcImagePath,0,256); //save image path
//    stSaveFileParam.iMethodValue = 1;

//    // ch:jpg图像质量范围为(50-99], png图像质量范围为[0-9] | en:jpg image nQuality range is (50-99], png image nQuality range is [0-9]
//    if (MV_Image_Bmp == stSaveFileParam.enImageType)
//    {
//        QString QstrSaveFileParam=QString::asprintf("Image_w%d_h%d_fn%03d.bmp",stSaveFileParam.nWidth, stSaveFileParam.nHeight, m_stImageInfo.nFrameNum);
//        memcpy(stSaveFileParam.pcImagePath,QstrSaveFileParam.toLatin1().data(),strlen(QstrSaveFileParam.toLatin1().data()));
//    }
//    else if (MV_Image_Jpeg == stSaveFileParam.enImageType)
//    {
//        QString QstrSaveFileParam=QString::asprintf("Image_w%d_h%d_fn%03d.jpg",stSaveFileParam.nWidth, stSaveFileParam.nHeight, m_stImageInfo.nFrameNum);
//        memcpy(stSaveFileParam.pcImagePath,QstrSaveFileParam.toLatin1().data(),strlen(QstrSaveFileParam.toLatin1().data()));
//        stSaveFileParam.nQuality = 80;
//    }
//    else if (MV_Image_Tif == stSaveFileParam.enImageType)
//    {
//        QString QstrSaveFileParam=QString::asprintf("Image_w%d_h%d_fn%03d.tif",stSaveFileParam.nWidth, stSaveFileParam.nHeight, m_stImageInfo.nFrameNum);
//        memcpy(stSaveFileParam.pcImagePath,QstrSaveFileParam.toLatin1().data(),strlen(QstrSaveFileParam.toLatin1().data()));
//    }
//    else if (MV_Image_Png == stSaveFileParam.enImageType)
//    {
//        stSaveFileParam.nQuality = 8;
//        QString QstrSaveFileParam=QString::asprintf("Image_w%d_h%d_fn%03d.png",stSaveFileParam.nWidth, stSaveFileParam.nHeight, m_stImageInfo.nFrameNum);
//        memcpy(stSaveFileParam.pcImagePath,QstrSaveFileParam.toLatin1().data(),strlen(QstrSaveFileParam.toLatin1().data()));
//    }

//    int nRet = m_pcMyCamera->SaveImageToFile(&stSaveFileParam);
//    pthread_mutex_unlock(&m_hSaveImageMux);/*解锁*/

//    qDebug() << QDir::currentPath();
//    return nRet;
//}
/**
 * @brief 保存当前帧为图片文件
 * @param enSaveImageType 保存的文件格式（BMP/JPEG/TIFF/PNG）
 * @return MV_OK 表示成功，其他为海康 SDK 错误码
 * @details
 *  - 使用 m_hSaveImageMux 保护 m_pSaveImageBuf / m_stImageInfo
 *  - 第一次保存时弹出对话框让用户选择保存目录，并缓存到 m_saveDir
 *  - 文件名包含时间戳：Image_yyyyMMdd_HHmmss.xxx
 *  - 通过 MV_SaveImageToFile 封装接口写文件
 */
int MainWindow::SaveImage(MV_SAVE_IAMGE_TYPE enSaveImageType)
{
    MV_SAVE_IMAGE_TO_FILE_PARAM_EX stSaveFileParam;
    memset(&stSaveFileParam, 0, sizeof(MV_SAVE_IMAGE_TO_FILE_PARAM_EX));

    // 先锁住，防止和回调线程同时访问 m_pSaveImageBuf / m_stImageInfo
    pthread_mutex_lock(&m_hSaveImageMux);

    // 没有数据就直接返回
    if (m_pSaveImageBuf == NULL || m_stImageInfo.enPixelType == 0)
    {
        pthread_mutex_unlock(&m_hSaveImageMux);
        return MV_E_NODATA;
    }

    // 填充图像相关参数（源数据）
    stSaveFileParam.enImageType = enSaveImageType;
    stSaveFileParam.enPixelType = m_stImageInfo.enPixelType;
    stSaveFileParam.nWidth      = m_stImageInfo.nWidth;
    stSaveFileParam.nHeight     = m_stImageInfo.nHeight;
    stSaveFileParam.nDataLen    = m_stImageInfo.nFrameLen;
    stSaveFileParam.pData       = m_pSaveImageBuf;
    stSaveFileParam.iMethodValue = 1;

    // 分配路径字符串缓存
    const int PATH_BUF_SIZE = 512;
    stSaveFileParam.pcImagePath = (char*)malloc(PATH_BUF_SIZE);
    if (stSaveFileParam.pcImagePath == NULL)
    {
        pthread_mutex_unlock(&m_hSaveImageMux);
        return MV_E_RESOURCE;
    }
    memset(stSaveFileParam.pcImagePath, 0, PATH_BUF_SIZE);

    // 第一次保存时，让用户选择保存目录
    if (m_saveDir.isEmpty())
    {
        QString dir = QFileDialog::getExistingDirectory(
            this,
            tr("选择保存文件夹"),
            QString(),
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

        // 用户取消选择
        if (dir.isEmpty())
        {
            free(stSaveFileParam.pcImagePath);
            pthread_mutex_unlock(&m_hSaveImageMux);
            return MV_E_PARAMETER;
        }

        m_saveDir = dir;
    }

    // 生成时间戳：Image_20250114_153050.jpg
    QString timeStr = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString filePath;

    switch (enSaveImageType)
    {
    case MV_Image_Bmp:
        filePath = QString("%1/Image_%2.bmp").arg(m_saveDir).arg(timeStr);
        break;

    case MV_Image_Jpeg:
        filePath = QString("%1/Image_%2.jpg").arg(m_saveDir).arg(timeStr);
        stSaveFileParam.nQuality = 80;   // JPG 质量，(50,99]
        break;

    case MV_Image_Tif:
        filePath = QString("%1/Image_%2.tif").arg(m_saveDir).arg(timeStr);
        break;

    case MV_Image_Png:
        filePath = QString("%1/Image_%2.png").arg(m_saveDir).arg(timeStr);
        stSaveFileParam.nQuality = 8;    // PNG 压缩级别，[0,9]
        break;

    default:
        free(stSaveFileParam.pcImagePath);
        pthread_mutex_unlock(&m_hSaveImageMux);
        return MV_E_PARAMETER;
    }

    // 把 QString 写入到 SDK 的 char* 缓冲区
    QByteArray ba = filePath.toLocal8Bit();
    qstrncpy(stSaveFileParam.pcImagePath, ba.constData(), PATH_BUF_SIZE - 1);
    stSaveFileParam.pcImagePath[PATH_BUF_SIZE - 1] = '\0';

    // 调用相机封装接口保存图片
    int nRet = m_pcMyCamera->SaveImageToFile(&stSaveFileParam);

    // 释放路径缓冲区
    free(stSaveFileParam.pcImagePath);

    pthread_mutex_unlock(&m_hSaveImageMux);
    return nRet;
}

/**
 * @brief 槽函数：保存 BMP 按钮
 */
void MainWindow::on_SaveBmpButton_clicked()
{
    int nRet = SaveImage(MV_Image_Bmp);
    if (MV_OK != nRet)
    {
        ShowErrorMsg("Save bmp fail", nRet);
        return;
    }
    ShowErrorMsg("Save bmp succeed", nRet);
}

/**
 * @brief 槽函数：保存 JPG 按钮
 */
void MainWindow::on_SaveJpgButton_clicked()
{
    int nRet = SaveImage(MV_Image_Jpeg);
    if (MV_OK != nRet)
    {
        ShowErrorMsg("Save jpg fail", nRet);
        return;
    }
    ShowErrorMsg("Save jpg succeed", nRet);
}

/**
 * @brief 槽函数：保存 TIFF 按钮
 */
void MainWindow::on_SaveTiffButton_clicked()
{
    int nRet = SaveImage(MV_Image_Tif);
    if (MV_OK != nRet)
    {
        ShowErrorMsg("Save tiff fail", nRet);
        return;
    }
    ShowErrorMsg("Save tiff succeed", nRet);
}

/**
 * @brief 槽函数：保存 PNG 按钮
 */
void MainWindow::on_SavePngButton_clicked()
{
    int nRet = SaveImage(MV_Image_Png);
    if (MV_OK != nRet)
    {
        ShowErrorMsg("Save png fail", nRet);
        return;
    }
    ShowErrorMsg("Save png succeed", nRet);
}


/**
 * @brief 槽函数：找圆心按钮（示例）
 * @details
 *  - 从当前帧缓存 m_frameBuffer 构建灰度图
 *  - 使用高斯模糊 + HoughCircles 进行圆检测
 *  - 若检测到至少一个圆，取第一个：
 *      - 在 labelCircleCenter / labelCircleRadius 上显示圆心坐标与半径
 *      - 画上圆轮廓与圆心点，弹窗展示检测结果
 *  - 若无检测结果，弹出提示
 *
 * @note 使用 QMutexLocker 保护 m_frameBuffer/m_frameInfo/m_hasFrame，
 *       避免与图像回调线程竞态。
 */
void MainWindow::on_bnFindCircle_clicked()
{
    // 1) 取当前帧灰度图
    QMutexLocker locker(&m_frameMutex);
    if (!m_hasFrame)
    {
        QMessageBox::information(this, tr("提示"), tr("当前没有可处理的图像帧"));
        return;
    }

    int imgW = m_frameInfo.nWidth;
    int imgH = m_frameInfo.nHeight;

    cv::Mat gray;
    if (m_frameInfo.enPixelType == PixelType_Gvsp_Mono8)
    {
        gray = cv::Mat(imgH, imgW, CV_8UC1, m_frameBuffer.data()).clone();
    }
    else if (m_frameInfo.enPixelType == PixelType_Gvsp_BGR8_Packed)
    {
        cv::Mat bgr(imgH, imgW, CV_8UC3, m_frameBuffer.data());
        cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    }
    else if (m_frameInfo.enPixelType == PixelType_Gvsp_RGB8_Packed)
    {
        cv::Mat rgb(imgH, imgW, CV_8UC3, m_frameBuffer.data());
        cv::cvtColor(rgb, gray, cv::COLOR_RGB2GRAY);
    }
    else
    {
        QMessageBox::warning(this, tr("提示"), tr("当前像素格式暂不支持圆检测"));
        return;
    }
    locker.unlock();

    if (gray.empty())
    {
        QMessageBox::warning(this, tr("提示"), tr("灰度图为空，无法检测"));
        return;
    }

    // 2) ROI：默认整图，有 ROI 就裁剪
    cv::Rect roiRect(0, 0, imgW, imgH);
    if (m_hasRoi)
    {
        QRectF r = currentRoiRect().normalized();
        int x = std::max(0, (int)std::floor(r.left()));
        int y = std::max(0, (int)std::floor(r.top()));
        int w = std::min(imgW - x, (int)std::ceil(r.width()));
        int h = std::min(imgH - y, (int)std::ceil(r.height()));
        if (w <= 10 || h <= 10)
        {
            QMessageBox::information(this, tr("提示"), tr("ROI 区域太小"));
            return;
        }
        roiRect = cv::Rect(x, y, w, h);
    }

    cv::Mat roiGray = gray(roiRect).clone();

    // 3) 如 ROI 很大，先缩小再算（加速）
    double scale = 1.0;
    const int maxSide = 1000; // ROI 最长边限制在 1000 像素左右
    int rw = roiRect.width;
    int rh = roiRect.height;
    int maxLen = std::max(rw, rh);
    cv::Mat workGray = roiGray;

    if (maxLen > maxSide)
    {
        scale = (double)maxSide / (double)maxLen;
        int newW = std::max(1, (int)std::round(rw * scale));
        int newH = std::max(1, (int)std::round(rh * scale));
        cv::resize(roiGray, workGray, cv::Size(newW, newH), 0, 0, cv::INTER_AREA);
    }

    // 4) 高斯模糊
    cv::GaussianBlur(workGray, workGray, cv::Size(7, 7), 1.5, 1.5);

    // 5) 判断是亮圈还是暗圈：比较整体均值和中心区域均值
    cv::Scalar globalMeanScalar = cv::mean(workGray);
    double globalMean = globalMeanScalar[0];

    int cx = workGray.cols / 2;
    int cy = workGray.rows / 2;
    int hw = workGray.cols / 5; // 20% 区域
    int hh = workGray.rows / 5;
    int x0 = std::max(0, cx - hw);
    int y0 = std::max(0, cy - hh);
    int w0 = std::min(workGray.cols  - x0, 2 * hw);
    int h0 = std::min(workGray.rows  - y0, 2 * hh);
    cv::Rect centerRect(x0, y0, w0, h0);

    double centerMean = cv::mean(workGray(centerRect))[0];

    int threshType;
    if (centerMean > globalMean)
        threshType = cv::THRESH_BINARY     | cv::THRESH_OTSU;  // 目标是亮圈
    else
        threshType = cv::THRESH_BINARY_INV | cv::THRESH_OTSU;  // 目标是暗圈

    // 6) 二值化 + 形态学闭运算
    cv::Mat bin;
    cv::threshold(workGray, bin, 0, 255, threshType);

    cv::Mat kernel = cv::getStructuringElement(
                         cv::MORPH_ELLIPSE, cv::Size(5, 5));
    cv::morphologyEx(bin, bin, cv::MORPH_CLOSE, kernel);

    // 7) 轮廓 + 最佳圆
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    cv::Point2f bestC_local(0, 0);  // 在缩小后的 ROI 坐标系
    float       bestR_local  = 0.0f;
    double      bestScore    = 0.0;

    const double PI = 3.141592653589793;

    // ROI 中心（在缩小后的坐标）
    cv::Point2f workCenter(workGray.cols * 0.5f,
                           workGray.rows * 0.5f);

    for (const auto &cnt : contours)
    {
        double area = cv::contourArea(cnt);
        if (area < 300) continue; // 忽略太小的

        cv::Point2f c;
        float r;
        cv::minEnclosingCircle(cnt, c, r);
        if (r < 15) continue;     // 半径太小忽略

        double circleArea = PI * r * r;
        double fillRatio  = area / circleArea;
        if (fillRatio < 0.3 || fillRatio > 1.7)
            continue;

        double roundness = std::min(fillRatio, 1.0 / fillRatio);

        // 距缩小后 ROI 中心的距离
        float dx = c.x - workCenter.x;
        float dy = c.y - workCenter.y;
        float dist = std::sqrt(dx*dx + dy*dy);

        float maxLenLocal = (float)std::max(workGray.cols, workGray.rows);
        float centerWeight = 1.0f - std::min(dist / maxLenLocal, 1.0f); // [0,1]

        // 打分：偏向大半径 + 靠中心 + 圆度好
        double score = r * r * (0.3 + 0.7 * centerWeight) * roundness;

        if (score > bestScore)
        {
            bestScore    = score;
            bestC_local  = c;
            bestR_local  = r;
        }
    }

    if (bestScore <= 0.0)
    {
        QMessageBox::information(this, tr("提示"), tr("未检测到圆，请调整 ROI 或光照"));
        return;
    }

    // 8) 把坐标从“缩小后的 ROI”映射回“整图”
    //    workGray = resize( roiGray, scale ) => original = local / scale
    float cxFull = bestC_local.x / (float)scale;
    float cyFull = bestC_local.y / (float)scale;
    float rFull  = bestR_local  / (float)scale;

    // 加上 ROI 左上角偏移
    cv::Point2f centerGlobal(
        cxFull + roiRect.x,
        cyFull + roiRect.y
    );

    // 9) 更新 UI 和叠加层
    ui->labelCircleCenter->setText(
                QString("(%1, %2)")
                .arg(centerGlobal.x, 0, 'f', 1)
                .arg(centerGlobal.y, 0, 'f', 1));
    ui->labelCircleRadius->setText(
                QString::number(rFull, 'f', 1));

    m_circleCenterOverlay = centerGlobal;
    m_circleRadiusOverlay = rFull;
    m_hasCircleOverlay    = true;

    emit sigNewFrame(m_displayImage);
}


///**
// * @brief 改进版：找圆/椭圆 (适配透视变形和纹理干扰)
// * @details
// * 1. 针对白底黑物使用 THRESH_BINARY_INV
// * 2. 放宽长宽比限制，允许检测透视造成的椭圆
// * 3. 增加形态学闭运算，消除树桩内部年轮纹理的干扰，只取最大轮廓
// */
//void MainWindow::on_bnFindCircle_clicked()
//{
//    QMutexLocker locker(&m_frameMutex);
//    if (!m_hasFrame) {
//        QMessageBox::information(this, tr("提示"), tr("当前没有可处理的图像帧"));
//        return;
//    }

//    int imgW = m_frameInfo.nWidth;
//    int imgH = m_frameInfo.nHeight;
//    cv::Mat gray;

//    // 1. 图像获取与灰度化
//    if (m_frameInfo.enPixelType == PixelType_Gvsp_Mono8) {
//        gray = cv::Mat(imgH, imgW, CV_8UC1, m_frameBuffer.data()).clone();
//    } else {
//        cv::Mat src(imgH, imgW, CV_8UC3, m_frameBuffer.data());
//        if (m_frameInfo.enPixelType == PixelType_Gvsp_BGR8_Packed)
//            cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
//        else
//            cv::cvtColor(src, gray, cv::COLOR_RGB2GRAY);
//    }
//    locker.unlock();

//    if (gray.empty()) return;

//    // 2. 预处理：高斯模糊去除年轮噪点
//    cv::Mat blurred;
//    cv::GaussianBlur(gray, blurred, cv::Size(9, 9), 2, 2);

//    // 3. 二值化 (关键修改：检测深色物体用 THRESH_BINARY_INV)
//    // 自动阈值，同时取反：让树桩变白(255)，背景变黑(0)
//    cv::Mat bin;
//    cv::threshold(blurred, bin, 0, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);

//    // 4. 形态学闭运算 (填补树桩内部的深色纹理空隙，使其成为一个实心块)
//    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(15, 15));
//    cv::morphologyEx(bin, bin, cv::MORPH_CLOSE, kernel);

//    // 5. 查找轮廓 (只找最外层轮廓)
//    std::vector<std::vector<cv::Point>> contours;
//    cv::findContours(bin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

//    cv::Point2f bestCenter;
//    float bestRadius = 0;
//    double maxArea = 0;
//    bool found = false;

//    for (const auto &cnt : contours)
//    {
//        double area = cv::contourArea(cnt);
//        if (area < 2000) continue; // 忽略噪点

//        if (cnt.size() < 5) continue; // 拟合椭圆至少需要5个点

//        // 6. 拟合椭圆
//        cv::RotatedRect rRect = cv::fitEllipse(cnt);

//        // 计算长短轴比 (Aspect Ratio)
//        double w = rRect.size.width;
//        double h = rRect.size.height;
//        double shortAxis = std::min(w, h);
//        double longAxis = std::max(w, h);
//        double ratio = shortAxis / longAxis;

//        // 计算面积填充率 (轮廓面积 / 拟合椭圆面积)
//        double ellipseArea = CV_PI * (w / 2.0) * (h / 2.0);
//        double fillRatio = area / ellipseArea;

//        // 7. 宽松的筛选条件 (关键修改)
//        // ratio > 0.4: 允许很扁的椭圆 (适配大角度透视)
//        // fillRatio > 0.85: 只要轮廓比较圆润光滑即可，不要求完美圆
//        if (ratio > 0.4 && fillRatio > 0.8 && fillRatio < 1.2)
//        {
//            // 取面积最大的那个
//            if (area > maxArea) {
//                maxArea = area;
//                bestCenter = rRect.center;
//                // 既然是椭圆，半径取长轴的一半，或者平均值，看你需要什么
//                // 这里取平均半径用于画圆示意，但画图时最好画椭圆
//                bestRadius = (w + h) / 4.0f;
//                found = true;
//            }
//        }
//    }

//    if (found) {
//        ui->labelCircleCenter->setText(QString("(%1, %2)").arg(bestCenter.x, 0, 'f', 1).arg(bestCenter.y, 0, 'f', 1));
//        ui->labelCircleRadius->setText(QString::number(bestRadius, 'f', 1));

//        // 更新显示覆盖层
//        m_circleCenterOverlay = bestCenter;
//        m_circleRadiusOverlay = bestRadius;
//        m_hasCircleOverlay = true;

//        emit sigNewFrame(m_displayImage);
//    } else {
//        QMessageBox::information(this, tr("提示"), tr("未检测到圆/椭圆\n请尝试调整光照或背景对比度"));
//    }
//}
/**
 * @brief 重度防粘连版：找盒子四点
 * @details
 * 1. 使用大尺寸核 (13x13) 进行强力腐蚀，强制断开靠得很近的毛绒物体
 * 2. 找出所有矩形，UI 上显示“最大且最实心”的那个
 * 3. 修复了 QVector 和 std::vector 类型转换问题
 */
void MainWindow::on_bnFindBox_clicked()
{
    QMutexLocker locker(&m_frameMutex);
    if (!m_hasFrame) {
        QMessageBox::information(this, tr("提示"), tr("无图像"));
        return;
    }

    int imgW = m_frameInfo.nWidth;
    int imgH = m_frameInfo.nHeight;
    cv::Mat gray;

    // 1) 灰度图
    if (m_frameInfo.enPixelType == PixelType_Gvsp_Mono8) {
        gray = cv::Mat(imgH, imgW, CV_8UC1, m_frameBuffer.data()).clone();
    } else {
        cv::Mat src(imgH, imgW, CV_8UC3, m_frameBuffer.data());
        if (m_frameInfo.enPixelType == PixelType_Gvsp_BGR8_Packed)
            cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
        else
            cv::cvtColor(src, gray, cv::COLOR_RGB2GRAY);
    }
    locker.unlock();

    if (gray.empty()) return;

    // 2) ROI
    cv::Rect roiRect(0, 0, imgW, imgH);
    if (m_hasRoi)
    {
        QRectF r = currentRoiRect().normalized();
        int x = std::max(0, (int)std::floor(r.left()));
        int y = std::max(0, (int)std::floor(r.top()));
        int w = std::min(imgW - x, (int)std::ceil(r.width()));
        int h = std::min(imgH - y, (int)std::ceil(r.height()));
        if (w <= 5 || h <= 5) {
            QMessageBox::information(this, tr("提示"), tr("ROI 区域太小"));
            return;
        }
        roiRect = cv::Rect(x, y, w, h);
    }

    cv::Mat roiGray = gray(roiRect).clone();
    cv::Point2f roiCenter(roiRect.x + roiRect.width  * 0.5f,
                          roiRect.y + roiRect.height * 0.5f);
    // 3) 高斯模糊
    cv::Mat blurred;
    cv::GaussianBlur(roiGray, blurred, cv::Size(9, 9), 2, 2);

    // 4) 两套二值图
    cv::Mat binBright, binDark;
    cv::threshold(blurred, binBright, 0, 255,
                  cv::THRESH_BINARY     | cv::THRESH_OTSU);
    cv::threshold(blurred, binDark,   0, 255,
                  cv::THRESH_BINARY_INV | cv::THRESH_OTSU);

    // 5) 重度防粘连核
    cv::Mat kernelHeavy   = cv::getStructuringElement(cv::MORPH_RECT,
                                                      cv::Size(13, 13));
    cv::Mat kernelRestore = cv::getStructuringElement(cv::MORPH_RECT,
                                                      cv::Size(15, 15));
    auto processMask = [&](cv::Mat mask,
                           const cv::Rect &roiRect,
                           const cv::Point2f &roiCenter,
                           std::vector<cv::Point2f> &allRectPoints,
                           std::vector<cv::Point2f> &maxRectPoints,
                           double &maxScore)
    {
        // 腐蚀+膨胀
        cv::erode(mask,  mask, kernelHeavy,   cv::Point(-1,-1), 2);
        cv::dilate(mask, mask, kernelRestore, cv::Point(-1,-1), 2);

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mask, contours, cv::RETR_EXTERNAL,
                         cv::CHAIN_APPROX_SIMPLE);

        for (const auto &cnt : contours)
        {
            double area = cv::contourArea(cnt);
            if (area < 1000) continue;

            cv::RotatedRect rRect = cv::minAreaRect(cnt);
            double rectArea = rRect.size.width * rRect.size.height;
            if (rectArea <= 1.0) continue;

            double solidity = area / rectArea;
            if (solidity < 0.6) continue;

            std::vector<cv::Point> approx;
            cv::approxPolyDP(cnt, approx,
                             0.04 * cv::arcLength(cnt, true), true);

            std::vector<cv::Point2f> quad;
            if (approx.size() == 4 && cv::isContourConvex(approx))
            {
                for (auto &p : approx)
                    quad.push_back(cv::Point2f(p.x, p.y));
            }
            else
            {
                cv::Point2f pts[4];
                rRect.points(pts);
                for (int i = 0; i < 4; ++i)
                    quad.push_back(pts[i]);
            }

            // 亚像素优化
            bool inBound = true;
            for (auto &p : quad)
            {
                if (p.x < 5 || p.y < 5 ||
                    p.x > mask.cols - 5 || p.y > mask.rows - 5)
                {
                    inBound = false;
                    break;
                }
            }
            if (inBound)
            {
                cv::TermCriteria criteria(
                    cv::TermCriteria::EPS + cv::TermCriteria::COUNT,
                    20, 0.01);
                cv::cornerSubPix(roiGray, quad,
                                 cv::Size(9, 9), cv::Size(-1, -1),
                                 criteria);
            }

            sortRectPoints(quad);

            // 映射到整图，同时求矩形中心
            cv::Point2f boxCenter(0.f, 0.f);
            for (auto &p : quad)
            {
                p.x += roiRect.x;
                p.y += roiRect.y;
                boxCenter += p;
            }
            boxCenter.x /= 4.0f;
            boxCenter.y /= 4.0f;

            float dx = boxCenter.x - roiCenter.x;
            float dy = boxCenter.y - roiCenter.y;
            float dist = std::sqrt(dx*dx + dy*dy);
            float maxLen = std::max(roiRect.width, roiRect.height);
            float centerWeight = 1.0f - std::min(dist / maxLen, 1.0f);

            // 只保留一个 score 相关变量
            double scoreShape = area * solidity;
            double score      = scoreShape * (0.3 + 0.7 * centerWeight);

            allRectPoints.insert(allRectPoints.end(),
                                 quad.begin(), quad.end());

            if (score > maxScore)
            {
                maxScore     = score;
                maxRectPoints = quad;
            }
        }
    };

    std::vector<cv::Point2f> allRectPoints;
    std::vector<cv::Point2f> maxRectPoints;
    double maxScore = 0.0;

    processMask(binBright, roiRect, roiCenter, allRectPoints, maxRectPoints, maxScore);
    processMask(binDark,   roiRect, roiCenter, allRectPoints, maxRectPoints, maxScore);

    if (allRectPoints.empty())
    {
        QMessageBox::information(this, tr("提示"),
                                 tr("未检测到有效矩形"));
        return;
    }

    // 更新 UI & 叠加
    if (maxRectPoints.size() == 4)
    {
        ui->labelBoxP1->setText(
                    QString("(%1, %2)")
                    .arg(maxRectPoints[0].x, 0, 'f', 1)
                    .arg(maxRectPoints[0].y, 0, 'f', 1));
        ui->labelBoxP2->setText(
                    QString("(%1, %2)")
                    .arg(maxRectPoints[1].x, 0, 'f', 1)
                    .arg(maxRectPoints[1].y, 0, 'f', 1));
        ui->labelBoxP3->setText(
                    QString("(%1, %2)")
                    .arg(maxRectPoints[2].x, 0, 'f', 1)
                    .arg(maxRectPoints[2].y, 0, 'f', 1));
        ui->labelBoxP4->setText(
                    QString("(%1, %2)")
                    .arg(maxRectPoints[3].x, 0, 'f', 1)
                    .arg(maxRectPoints[3].y, 0, 'f', 1));
    }

    {
        QMutexLocker locker2(&m_frameMutex);
        m_lastQuad = maxRectPoints;
        m_hasQuad  = true;

        m_boxPointsOverlay.clear();
        for (auto &p : allRectPoints)
            m_boxPointsOverlay.push_back(p);
        m_hasBoxOverlay = true;
    }

    emit sigNewFrame(m_displayImage);
}



/**
 * @brief 槽函数：畸变矫正（平面透视矫正）
 * @details
 *  - 依赖最近一次“找盒子四点”的结果 m_lastQuad：
 *      - quad[0..3] 对应 [左上, 右上, 左下, 右下] 图像坐标
 *  - 根据四点计算目标矩形宽高（取两条上边/下边、左边/右边的最大长度）
 *  - 构建源点 srcPts[4] 与目标点 dstPts[4]
 *  - 使用 getPerspectiveTransform + warpPerspective
 *    将倾斜的矩形区域拉正为一个正矩形图像
 *  - 拉正结果使用 QLabel 弹窗显示
 *
 * @note
 *  - 这是平面透视矫正，与相机内参畸变校正（桶形/枕形）不同；
 *    后者需要标定。
 */
void MainWindow::on_bnRectify_clicked()
{
    // 1. 先确认有当前帧和上一次的矩形四点
    QMutexLocker locker(&m_frameMutex);
    if (!m_hasFrame)
    {
        QMessageBox::information(this, tr("提示"), tr("当前没有图像帧，无法矫正"));
        return;
    }
    if (!m_hasQuad || m_lastQuad.size() != 4)
    {
        QMessageBox::information(this, tr("提示"), tr("请先执行一次“找盒子四点”"));
        return;
    }

    int imgW = m_frameInfo.nWidth;
    int imgH = m_frameInfo.nHeight;

    // 根据像素格式构建一个 BGR 图，用于后面 warpPerspective + 显示
    cv::Mat src;

    if (m_frameInfo.enPixelType == PixelType_Gvsp_Mono8)
    {
        cv::Mat gray(imgH, imgW, CV_8UC1, m_frameBuffer.data());
        cv::cvtColor(gray, src, cv::COLOR_GRAY2BGR);
    }
    else if (m_frameInfo.enPixelType == PixelType_Gvsp_BGR8_Packed)
    {
        src = cv::Mat(imgH, imgW, CV_8UC3, m_frameBuffer.data()).clone();
    }
    else if (m_frameInfo.enPixelType == PixelType_Gvsp_RGB8_Packed)
    {
        cv::Mat rgb(imgH, imgW, CV_8UC3, m_frameBuffer.data());
        cv::cvtColor(rgb, src, cv::COLOR_RGB2BGR);
    }
    else
    {
        QMessageBox::warning(this, tr("提示"), tr("当前像素格式暂不支持畸变矫正"));
        return;
    }

    // 拿到四点的副本，避免后面改动 m_lastQuad
    std::vector<cv::Point2f> quad = m_lastQuad;
    locker.unlock();   // 后面不再访问共享数据，可以提前放锁

    if (quad.size() != 4)
    {
        QMessageBox::information(this, tr("提示"), tr("矩形四点数据不完整"));
        return;
    }

    // 2. 计算目标矩形的宽、高（取对应边长的最大值）
    auto dist = [](const cv::Point2f &a, const cv::Point2f &b) {
        return std::sqrt((a.x - b.x)*(a.x - b.x) + (a.y - b.y)*(a.y - b.y));
    };

    // 注意：这里假设 sortRectPoints 的顺序是 [左上, 右上, 左下, 右下]
    float w1 = dist(quad[0], quad[1]);
    float w2 = dist(quad[2], quad[3]);
    float h1 = dist(quad[0], quad[2]);
    float h2 = dist(quad[1], quad[3]);

    int dstW = (int)std::round(std::max(w1, w2));
    int dstH = (int)std::round(std::max(h1, h2));

    if (dstW <= 0 || dstH <= 0)
    {
        QMessageBox::warning(this, tr("提示"), tr("畸形矩形，无法计算目标尺寸"));
        return;
    }

    // 3. 准备透视变换的源点和目标点
    cv::Point2f srcPts[4];
    cv::Point2f dstPts[4];

    // 源点：按照 [左上, 右上, 左下, 右下] 的顺序
    srcPts[0] = quad[0];
    srcPts[1] = quad[1];
    srcPts[2] = quad[2];
    srcPts[3] = quad[3];

    // 目标点：映射到一个正的矩形
    dstPts[0] = cv::Point2f(0.0f,       0.0f);        // 左上
    dstPts[1] = cv::Point2f(dstW - 1.f, 0.0f);        // 右上
    dstPts[2] = cv::Point2f(0.0f,       dstH - 1.f);  // 左下
    dstPts[3] = cv::Point2f(dstW - 1.f, dstH - 1.f);  // 右下

    // 4. 计算透视变换矩阵并矫正
    cv::Mat M = cv::getPerspectiveTransform(srcPts, dstPts);
    cv::Mat warped;
    cv::warpPerspective(src, warped, M, cv::Size(dstW, dstH));

    if (warped.empty())
    {
        QMessageBox::warning(this, tr("提示"), tr("畸变矫正失败（结果为空）"));
        return;
    }

    // 5. 弹窗显示矫正后的结果
    QImage qimg(warped.data,
                warped.cols,
                warped.rows,
                warped.step,
                QImage::Format_BGR888);
    QPixmap pix = QPixmap::fromImage(qimg.copy());

    QLabel *dlg = new QLabel;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("畸变矫正结果"));
    dlg->setPixmap(pix);
    dlg->resize(pix.size());
    dlg->show();
}


/**
 * @brief “开始九点标定” 按钮
 * @details
 *   - 清空已有标定点
 *   - 置 m_isCalibrating = true，等待用户在图像上依次点击 9 次
 */
void MainWindow::on_bnStartCalib9_clicked()
{
    // 先确保当前有一帧图像
    {
        QMutexLocker locker(&m_frameMutex);
        if (!m_hasFrame)
        {
            QMessageBox::warning(this, tr("提示"),
                                 tr("当前没有图像，无法进入九点标定"));
            return;
        }

        // 记录开始标定时的图像尺寸，用于“图像变化保护”
        m_calibImgWidth  = m_frameInfo.nWidth;
        m_calibImgHeight = m_frameInfo.nHeight;
    }

    m_calibPoints.clear();
    m_isCalibrating = true;
    updateCalibLabels();

    QMessageBox::information(this, tr("提示"),
                             tr("已进入九点标定模式，请在图像上依次点击 9 个点"));
}


/**
 * @brief “清除标定点” 按钮
 * @details
 *   - 清空 m_calibPoints，保留当前模式（不强制进入标定）
 */
void MainWindow::on_bnClearCalib9_clicked()
{
    m_calibPoints.clear();
    updateCalibLabels();
    m_isCalibrating = false;
}

/**
 * @brief “保存标定结果” 按钮
 * @details
 *   - 要求必须已经采集 9 个点
 *   - 弹出文件保存对话框，默认 CSV 名：Calib9_yyyyMMdd_HHmmss.csv
 *   - 文件格式：
 *       Index,X,Y
 *       1, x1, y1
 *       ...
 *       9, x9, y9
 */
void MainWindow::on_bnSaveCalib9_clicked()
{
    if (m_calibPoints.size() != 9)
    {
        QMessageBox::warning(this, tr("提示"),
                             tr("当前标定点数量不是 9 个，无法保存"));
        return;
    }

    QString defaultName =
        QString("Calib9_%1.csv")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));

    QString filePath = QFileDialog::getSaveFileName(
                this,
                tr("保存九点标定结果"),
                defaultName,
                tr("CSV 文件 (*.csv);;所有文件 (*)"));

    if (filePath.isEmpty())
        return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("错误"),
                             tr("无法打开文件进行写入"));
        return;
    }

    QTextStream out(&file);
    out.setCodec("UTF-8");

    // 写入表头
    out << "Index,X,Y\n";
    for (int i = 0; i < m_calibPoints.size(); ++i)
    {
        out << (i + 1) << ","
            << m_calibPoints[i].x << ","
            << m_calibPoints[i].y << "\n";
    }

    file.close();

    QMessageBox::information(this, tr("提示"),
                             tr("九点标定结果已保存到:\n%1").arg(filePath));
}

//clear all button
void MainWindow::on_bnClearOverlay_clicked()
{
    clearAllOverlays();
}

void MainWindow::on_btnOpenImage_clicked()
{
    m_hasCircleOverlay = false;
    m_hasBoxOverlay    = false;
    // 建议只在没有采集时允许打开本地图片
    if (m_bGrabbing)
    {
        QMessageBox::warning(this, tr("提示"),
                             tr("请先停止采集，再打开本地图片"));
        return;
    }

    QString fileName = QFileDialog::getOpenFileName(
                this,
                tr("选择图片"),
                QString(),
                tr("图像文件 (*.bmp *.jpg *.jpeg *.png *.tif *.tiff);;所有文件 (*.*)"));

    if (fileName.isEmpty())
        return;

    QImage img(fileName);
    if (img.isNull())
    {
        QMessageBox::warning(this, tr("提示"),
                             tr("加载图片失败：%1").arg(fileName));
        return;
    }

    // 把这张图当作“当前帧”载入并显示
    loadImageAsCurrentFrame(img);

    QMessageBox::information(this, tr("提示"),
                             tr("已加载图片：\n%1\n"
                                "现在可以在此图片上进行九点标定、找圆、找盒子等操作。")
                                .arg(fileName));
}




/**
 * @brief 事件过滤器：处理 DisplayWidget 视图中的缩放和鼠标查询
 * @param watched  被监控的对象，这里是 ui->DisplayWidget->viewport()
 * @param event    事件指针（鼠标移动/滚轮等）
 * @details
 *  - 对于 Wheel 事件：
 *      - 使用 scale() 实现滚轮放大/缩小
 *  - 对于 MouseMove 事件：
 *      - 将 viewport 坐标映射到场景坐标（图像坐标）
 *      - 在当前帧缓存中读取像素灰度值：
 *          - Mono8：直接取该点单通道值
 *          - BGR/RGB：将三个通道按权重转换为灰度
 *      - 更新 labelCoord / labelGray 显示当前坐标和灰度
 *  - 其他事件交给父类默认处理
 *
 * @note
 *  - 使用 QMutexLocker 保护 m_frameBuffer/m_frameInfo/m_hasFrame，
 *    保证和回调线程的并发安全。
 */
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != ui->DisplayWidget->viewport())
        return QMainWindow::eventFilter(watched, event);

    // =========================
    // 1) 滚轮缩放
    // =========================
    if (event->type() == QEvent::Wheel)
    {
        QWheelEvent *we = static_cast<QWheelEvent *>(event);
        const double scaleFactor = 1.25;

        if (we->angleDelta().y() > 0)
            ui->DisplayWidget->scale(scaleFactor, scaleFactor);      // 放大
        else
            ui->DisplayWidget->scale(1.0 / scaleFactor, 1.0 / scaleFactor); // 缩小

        return true; // 已处理
    }

    // =========================
    // 2) 鼠标移动：坐标 + 灰度 + ROI 拖动 + 测距拖动
    // =========================
    if (event->type() == QEvent::MouseMove)
    {
        QMouseEvent *me = static_cast<QMouseEvent *>(event);
        QPointF scenePos = ui->DisplayWidget->mapToScene(me->pos());

        // ---- ROI 拖动：只在非标定模式下生效 ----
        if (!m_isCalibrating &&
            m_isSelectingRoi &&
            (me->buttons() & Qt::LeftButton))
        {
            m_roiEnd = cv::Point2f((float)scenePos.x(), (float)scenePos.y());
            emit sigNewFrame(m_displayImage);  // 重绘显示矩形
        }

        // ---- 测距拖动：也只在非标定模式下 ----
        if (!m_isCalibrating && m_isDraggingMeasure)
        {
            m_measureEnd = cv::Point2f((float)scenePos.x(), (float)scenePos.y());
            emit sigNewFrame(m_displayImage); // 触发重绘，显示动态线段
        }

        int x = qRound(scenePos.x());
        int y = qRound(scenePos.y());

        QMutexLocker locker(&m_frameMutex);
        if (m_hasFrame &&
            x >= 0 && x < m_frameInfo.nWidth &&
            y >= 0 && y < m_frameInfo.nHeight)
        {
            int gray = 0;

            if (m_frameInfo.enPixelType == PixelType_Gvsp_Mono8)
            {
                int idx = y * m_frameInfo.nWidth + x;
                if (idx >= 0 && idx < (int)m_frameBuffer.size())
                    gray = m_frameBuffer[idx];
            }
            else if (m_frameInfo.enPixelType == PixelType_Gvsp_BGR8_Packed ||
                     m_frameInfo.enPixelType == PixelType_Gvsp_RGB8_Packed)
            {
                int idx = (y * m_frameInfo.nWidth + x) * 3;
                if (idx + 2 < (int)m_frameBuffer.size())
                {
                    int b = m_frameBuffer[idx + 0];
                    int g = m_frameBuffer[idx + 1];
                    int r = m_frameBuffer[idx + 2];
                    gray = (r * 30 + g * 59 + b * 11) / 100;
                }
            }

            ui->labelCoord->setText(QString("(%1, %2)").arg(x).arg(y));
            ui->labelGray->setText(QString::number(gray));
        }

        return false; // 保留 Qt 自己的拖拽行为
    }

    // =========================
    // 3) 鼠标按下：先处理九点标定，再处理 ROI / 测距
    // =========================
    if (event->type() == QEvent::MouseButtonPress)
    {
        QMouseEvent *me = static_cast<QMouseEvent *>(event);
        QPointF scenePos = ui->DisplayWidget->mapToScene(me->pos());

        // --- 情况 A：九点标定模式（独占左键，不影响 ROI） ---
        if (m_isCalibrating)
        {
            if (me->button() == Qt::LeftButton)
            {
                float x = (float)scenePos.x();
                float y = (float)scenePos.y();

                QMutexLocker locker(&m_frameMutex);
                if (!(m_hasFrame &&
                      x >= 0 && x < m_frameInfo.nWidth &&
                      y >= 0 && y < m_frameInfo.nHeight))
                {
                    return false;
                }

                // 读取灰度
                int gray = 0;
                if (m_frameInfo.enPixelType == PixelType_Gvsp_Mono8)
                {
                    int idx = (int)(y + 0.5f) * m_frameInfo.nWidth + (int)(x + 0.5f);
                    if (idx >= 0 && idx < (int)m_frameBuffer.size())
                        gray = m_frameBuffer[idx];
                }
                else if (m_frameInfo.enPixelType == PixelType_Gvsp_BGR8_Packed ||
                         m_frameInfo.enPixelType == PixelType_Gvsp_RGB8_Packed)
                {
                    int idx = ((int)(y + 0.5f) * m_frameInfo.nWidth + (int)(x + 0.5f)) * 3;
                    if (idx + 2 < (int)m_frameBuffer.size())
                        gray = (m_frameBuffer[idx] * 30 +
                                m_frameBuffer[idx + 1] * 59 +
                                m_frameBuffer[idx + 2] * 11) / 100;
                }

                // 灰度阈值检查
                const int grayThreshold = 30;
                if (gray < grayThreshold)
                {
                    locker.unlock();
                    QMessageBox::warning(this, tr("提示"), tr("当前选择点灰度太低，请点击在发光区域/目标上"));
                    return false;
                }

                // 距离保护
                cv::Point2f newPt(x, y);
                float minDist2 = m_calibMinDist * m_calibMinDist;

                for (int i = 0; i < m_calibPoints.size(); ++i)
                {
                    float dx = newPt.x - m_calibPoints[i].x;
                    float dy = newPt.y - m_calibPoints[i].y;
                    if (dx * dx + dy * dy < minDist2)
                    {
                        locker.unlock();
                        QMessageBox::warning(this, tr("提示"), tr("当前点与第 %1 个点太近").arg(i + 1));
                        return false;
                    }
                }

                // 加入点
                if (m_calibPoints.size() < 9)
                {
                    m_calibPoints.push_back(newPt);
                    locker.unlock();
                    updateCalibLabels();
                    if (m_calibPoints.size() == 9)
                    {
                        m_isCalibrating = false;
                        QMessageBox::information(this, tr("提示"), tr("九个点已经采集完成"));
                    }
                }

                emit sigNewFrame(m_displayImage); // 刷新显示标定点
                return true;                      // 🔴 标定模式下不再走 ROI / 测距
            }

            return false;
        }

        // --- 情况 B：非标定模式：ROI + 测距 ---

        // Ctrl + 左键 开始 ROI 框选
        if (me->button() == Qt::LeftButton &&
            (me->modifiers() & Qt::ControlModifier))
        {
            m_isSelectingRoi = true;
            m_hasRoi         = true;
            m_roiStart       = cv::Point2f((float)scenePos.x(), (float)scenePos.y());
            m_roiEnd         = m_roiStart;

            emit sigNewFrame(m_displayImage);
            return true;
        }

        // 左键：开始测量
        if (me->button() == Qt::LeftButton)
        {
            m_measureStart       = cv::Point2f((float)scenePos.x(), (float)scenePos.y());
            m_measureEnd         = m_measureStart;
            m_isMeasuring        = true;
            m_isDraggingMeasure  = true;
            return true;
        }
        // 右键：取消测量
        else if (me->button() == Qt::RightButton)
        {
            m_isMeasuring       = false;
            m_isDraggingMeasure = false;
            emit sigNewFrame(m_displayImage);
            return true;
        }

        return false;
    }

    // =========================
    // 4) 鼠标释放：结束 ROI / 测距
    // =========================
    if (event->type() == QEvent::MouseButtonRelease)
    {
        QMouseEvent *me = static_cast<QMouseEvent *>(event);

        // 结束 ROI 框选
        if (me->button() == Qt::LeftButton && m_isSelectingRoi)
        {
            m_isSelectingRoi = false;

            QRectF r = currentRoiRect();
            if (r.width() < 5 || r.height() < 5)
                m_hasRoi = false;   // 太小就当作取消

            emit sigNewFrame(m_displayImage);
            return true;
        }

        // 结束测距拖拽
        if (me->button() == Qt::LeftButton && m_isDraggingMeasure)
        {
            m_isDraggingMeasure = false;
            emit sigNewFrame(m_displayImage);
            return true;
        }

        return false;
    }

    return QMainWindow::eventFilter(watched, event);
}


/**
 * @brief 槽函数：接收图像回调线程发来的新帧 QImage
 * @param img 当前帧图像
 * @details
 *  - 由 sigNewFrame 触发（Qt queued connection，跨线程）
 *  - 将 QImage 保存到 m_displayImage，并更新 m_pixItem 的 QPixmap
 *  - 调整场景 rect，使图像完整显示在 QGraphicsView 中
 */
//void MainWindow::onNewFrame(const QImage &img)
//{
//    if (img.isNull())
//        return;

//    m_displayImage = img;

//    // === 图像变化保护：如果分辨率变化了，清空当前标定点 ===
//    if (m_calibImgWidth != 0 && m_calibImgHeight != 0)
//    {
//        int curW = img.width();
//        int curH = img.height();
//        if ((curW != m_calibImgWidth || curH != m_calibImgHeight) &&
//            !m_calibPoints.isEmpty())
//        {
//            m_calibPoints.clear();
//            m_isCalibrating = false;
//            updateCalibLabels();

//            QMessageBox::information(this, tr("提示"),
//                                     tr("图像尺寸已变化，九点标定点已自动清除，请重新标定"));
//            // 重置记录的尺寸
//            m_calibImgWidth  = curW;
//            m_calibImgHeight = curH;
//        }
//    }

//    QPixmap pix = QPixmap::fromImage(m_displayImage);

//    // 叠加九点标定可视化
//    if (!m_calibPoints.isEmpty())
//    {
//        QPainter painter(&pix);
//        painter.setRenderHint(QPainter::Antialiasing, true);

//        QPen pen(Qt::red);
//        pen.setWidth(3);
//        painter.setPen(pen);
//        painter.setBrush(Qt::NoBrush);

//        for (int i = 0; i < m_calibPoints.size(); ++i)
//        {
//            QPointF pt(m_calibPoints[i].x, m_calibPoints[i].y);
//            painter.drawEllipse(pt, 5, 5);
//            painter.drawText(pt + QPointF(6, -6),
//                             QString::number(i + 1));
//        }
//    }

//    m_pixItem->setPixmap(pix);
//    m_scene->setSceneRect(pix.rect());
//}
void MainWindow::onNewFrame(const QImage &img)
{
    if (img.isNull()) return;
    m_displayImage = img;
    QPixmap pix = QPixmap::fromImage(m_displayImage);

    if (!pix.isNull())
    {
        QPainter painter(&pix);
        painter.setRenderHint(QPainter::Antialiasing, true);

        // ==========================================
        // 1. [新功能] 交互式测距 (Ruler) 显示逻辑
        // ==========================================
        if (m_isMeasuring)
        {
            QPen penMeasure(Qt::cyan); // 青色虚线
            penMeasure.setWidth(2);
            penMeasure.setStyle(Qt::DashLine);
            painter.setPen(penMeasure);

            QPointF p1(m_measureStart.x, m_measureStart.y);
            QPointF p2(m_measureEnd.x, m_measureEnd.y);

            // 画线
            painter.drawLine(p1, p2);

            // 画端点（小圆圈）
            painter.setBrush(Qt::cyan);
            painter.drawEllipse(p1, 4, 4);
            painter.drawEllipse(p2, 4, 4);

            // --- 核心：计算并显示距离 ---
            float dx = m_measureStart.x - m_measureEnd.x;
            float dy = m_measureStart.y - m_measureEnd.y;
            float dist = std::sqrt(dx*dx + dy*dy); // 勾股定理算距离

            // 在线段中间偏上的位置显示文字
            QPointF midPoint = (p1 + p2) / 2;

            // 设置大号字体，黄色，加粗，确保能看清
            painter.setPen(Qt::yellow);
            QFont font = painter.font();
            font.setPixelSize(24); // 字号调大
            font.setBold(true);
            painter.setFont(font);

            // 显示格式： "Distance: 123.5 px"
            painter.drawText(midPoint + QPointF(10, -10), QString("Dist: %1 px").arg(dist, 0, 'f', 1));
        }

        // ==========================================
        // X. 新增：画 ROI 框 (半透明绿色矩形)
        // ==========================================
        if (m_hasRoi)
        {
            QRectF r = currentRoiRect();
            if (r.width() > 1 && r.height() > 1)
            {
                QPen penRoi(Qt::green);
                penRoi.setWidth(2);
                penRoi.setStyle(Qt::DashLine);
                painter.setPen(penRoi);

                QColor fill(0, 255, 0, 40); // 半透明绿色
                painter.setBrush(fill);

                painter.drawRect(r);
            }
        }

        // ==========================================
        // 2. 画所有检测到的矩形 (黄色细线)
        // ==========================================
        if (m_hasBoxOverlay && !m_boxPointsOverlay.isEmpty())
        {
            QPen penAll(Qt::yellow);
            penAll.setWidth(1);
            penAll.setStyle(Qt::SolidLine);
            painter.setPen(penAll);
            painter.setBrush(Qt::NoBrush);

            for (int i = 0; i < m_boxPointsOverlay.size(); i += 4)
            {
                if (i + 3 >= m_boxPointsOverlay.size()) break;
                QPointF p1(m_boxPointsOverlay[i+0].x, m_boxPointsOverlay[i+0].y);
                QPointF p2(m_boxPointsOverlay[i+1].x, m_boxPointsOverlay[i+1].y);
                QPointF p3(m_boxPointsOverlay[i+2].x, m_boxPointsOverlay[i+2].y);
                QPointF p4(m_boxPointsOverlay[i+3].x, m_boxPointsOverlay[i+3].y);

                QPolygonF poly;
                poly << p1 << p2 << p3 << p4;
                painter.drawPolygon(poly);
            }
        }

        // ==========================================
        // 3. 画当前选中的最大矩形 (红色粗线)
        // ==========================================
        if (m_hasQuad && m_lastQuad.size() == 4)
        {
            QPen penSelected(Qt::red);
            penSelected.setWidth(3);
            painter.setPen(penSelected);

            QPointF p1(m_lastQuad[0].x, m_lastQuad[0].y);
            QPointF p2(m_lastQuad[1].x, m_lastQuad[1].y);
            QPointF p3(m_lastQuad[2].x, m_lastQuad[2].y);
            QPointF p4(m_lastQuad[3].x, m_lastQuad[3].y);

            QPolygonF poly;
            poly << p1 << p2 << p3 << p4;
            painter.drawPolygon(poly);

            // 标出 P1~P4
            painter.setPen(Qt::green);
            QFont font = painter.font();
            font.setPixelSize(16);
            painter.setFont(font);
            painter.drawText(p1, "P1");
            painter.drawText(p2, "P2");
            painter.drawText(p3, "P3");
            painter.drawText(p4, "P4");
        }

        // ==========================================
        // 4. 其他绘制 (圆、标定点、FPS)
        // ==========================================
        if (m_hasCircleOverlay && m_circleRadiusOverlay > 0.0f) {
             QPen penCircle(Qt::red); penCircle.setWidth(3); painter.setPen(penCircle);
             QPointF c(m_circleCenterOverlay.x, m_circleCenterOverlay.y);
             painter.drawEllipse(c, m_circleRadiusOverlay, m_circleRadiusOverlay);
        }

        if (!m_calibPoints.isEmpty()) {
             QPen penCalib(Qt::green); penCalib.setWidth(3); painter.setPen(penCalib);
             for (int i=0; i<m_calibPoints.size(); ++i) {
                 QPointF pt(m_calibPoints[i].x, m_calibPoints[i].y);
                 painter.drawEllipse(pt, 5, 5);
                 painter.drawText(pt + QPointF(6, -6), QString::number(i + 1));
             }
        }

        // FPS 计算
        m_frameCountForFps++;
        if (m_fpsTimer.elapsed() >= 1000) {
            m_currentFps = m_frameCountForFps * 1000.0 / m_fpsTimer.elapsed();
            m_frameCountForFps = 0;
            m_fpsTimer.restart();
            ui->labelFps->setText(QString::number(m_currentFps, 'f', 1));
        }

        // 平均灰度
        QImage grayImg = m_displayImage.convertToFormat(QImage::Format_Grayscale8);
        qint64 sum = 0;
        int step = 4;
        for (int y = 0; y < grayImg.height(); y+=step) {
            const uchar *line = grayImg.constScanLine(y);
            for (int x = 0; x < grayImg.width(); x+=step) sum += line[x];
        }
        double meanGray = sum / double((grayImg.width()/step) * (grayImg.height()/step));
        ui->labelMeanGray->setText(QString::number(meanGray, 'f', 1));

        // ==========================================
        // 5. 中心十字准心（始终画在图像中心）
        // ==========================================
        {
            // 以当前显示图像的宽高作为坐标系
            double cx = pix.width()  / 2.0;
            double cy = pix.height() / 2.0;

            QPen penCross(Qt::green); // 或 Qt::red，看你喜好
            penCross.setWidth(1);
            penCross.setStyle(Qt::DashLine); // 虚线准心，如果想实线可去掉这句
            painter.setPen(penCross);
            painter.setBrush(Qt::NoBrush);

            // 垂直线
            painter.drawLine(QPointF(cx, 0), QPointF(cx, pix.height()));
            // 水平线
            painter.drawLine(QPointF(0, cy), QPointF(pix.width(), cy));

            // 也可以在中心画一个小圆点，方便看中心位置
            // painter.setBrush(Qt::green);
            // painter.drawEllipse(QPointF(cx, cy), 3, 3);
        }
    }

    m_pixItem->setPixmap(pix);
    m_scene->setSceneRect(pix.rect());
}
