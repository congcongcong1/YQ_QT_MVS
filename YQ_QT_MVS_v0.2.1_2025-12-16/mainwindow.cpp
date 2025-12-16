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
#include <QInputDialog> // 【关键】QInputDialog 弹窗所需
#include <QRegularExpression>
#include <QDir>             // 【新增】解决 'QDir' 未声明的错误
#include <QMetaType>  // <--- 【新增这行】
using namespace cv;


// 辅助结构体，用于存储圆检测结果并实现自定义排序
struct CircleResult {
    Point2f center;
    float radius;

    // 自定义排序操作符：实现“从上到下、从左到右”的排序
    bool operator<(const CircleResult& other) const {
        // Y 坐标容忍度，用于判断是否在同一行（可根据实际情况调整）
        // 如果圆心Y坐标差值小于此值，则认为在同一行
        const float Y_TOLERANCE = 20.0f;

        // 1. 先按 Y 坐标排序 (从上到下)
        if (std::abs(center.y - other.center.y) > Y_TOLERANCE) {
            return center.y < other.center.y;
        }
        // 2. 如果 Y 坐标近似，则按 X 坐标排序 (从左到右)
        return center.x < other.center.x;
    }
};





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
    // UI 控件数组，用于批量访问
    QLabel* worldInputs[] = {
        ui->labelCalibP1, ui->labelCalibP2, ui->labelCalibP3,
        ui->labelCalibP4, ui->labelCalibP5, ui->labelCalibP6,
        ui->labelCalibP7, ui->labelCalibP8, ui->labelCalibP9
    };

    // 清空或更新标签
    for (int i = 0; i < 9; ++i)
    {
        if (i < m_calibPoints.size())
        {
            // 显示已采集的世界坐标 (x, y)
            const cv::Point2f& p = m_calibPoints[i];
            QString str = QString("(%1, %2)").arg(p.x, 0, 'f', 4).arg(p.y, 0, 'f', 4);
            worldInputs[i]->setText(str);
        }
        else
        {
            // 清空未采集的标签
            worldInputs[i]->setText(tr("---"));
        }
    }
}

/**
 * @brief 辅助函数：根据 m_circleCenterOverlay 和 m_circleRadiusOverlay
 * 刷新多圆检测结果的坐标和半径显示。
 * @details 遍历 m_circleCenterOverlay 和 m_circleRadiusOverlay 容器，
 * 将 P1 到 P9 的圆心坐标和半径显示在对应的 UI 标签上。
 * @note 标签假定命名为 labelCircleCenter_1 到 9 和 labelCircleRadius_1 到 9。
 */
void MainWindow::updateCircleIfoLables()
{
    // -------------------------------------------------------------
    // 辅助 Lambda：设置圆心标签 (P#: (x.x, y.y))
    // -------------------------------------------------------------
    auto setCenterLabel = [](QLabel *lbl, int idx, const QVector<cv::Point2f> &pts)
    {
        if (!lbl) return;
        if (idx < pts.size())
        {
            lbl->setText(
                QString("P%1: (%2, %3)")
                    .arg(idx + 1)
                    .arg(pts[idx].x, 0, 'f', 1) // 坐标保留 1 位小数
                    .arg(pts[idx].y, 0, 'f', 1));
        }
        else
        {
            lbl->setText(
                QString("P%1: ---").arg(idx + 1));
        }
    };

    // -------------------------------------------------------------
    // 辅助 Lambda：设置半径标签 (R#: r.r)
    // -------------------------------------------------------------
    auto setRadiusLabel = [](QLabel *lbl, int idx, const QVector<float> &radii)
    {
        if (!lbl) return;
        if (idx < radii.size())
        {
            lbl->setText(
                QString("R%1: %2")
                    .arg(idx + 1)
                    .arg(radii[idx], 0, 'f', 1)); // 半径保留 1 位小数
        }
        else
        {
            lbl->setText(
                QString("R%1: ---").arg(idx + 1));
        }
    };

    // -------------------------------------------------------------
    // 统一更新 9 组标签
    // -------------------------------------------------------------

    // 假设您的 UI 控件命名遵循以下模式：
    // 圆心标签: ui->labelCircleCenter_1 到 ui->labelCircleCenter_9
    // 半径标签: ui->labelCircleRadius_1 到 ui->labelCircleRadius_9

    // --- P1/R1 ---
    setCenterLabel(ui->labelCircleCenter, 0, m_circleCenterOverlay);
    setRadiusLabel(ui->labelCircleRadius, 0, m_circleRadiusOverlay);

    // --- P2/R2 ---
    setCenterLabel(ui->labelCircleCenter_2, 1, m_circleCenterOverlay);
    setRadiusLabel(ui->labelCircleRadius_2, 1, m_circleRadiusOverlay);

    // --- P3/R3 ---
    setCenterLabel(ui->labelCircleCenter_3, 2, m_circleCenterOverlay);
    setRadiusLabel(ui->labelCircleRadius_3, 2, m_circleRadiusOverlay);

    // --- P4/R4 ---
    setCenterLabel(ui->labelCircleCenter_4, 3, m_circleCenterOverlay);
    setRadiusLabel(ui->labelCircleRadius_4, 3, m_circleRadiusOverlay);

    // --- P5/R5 ---
    setCenterLabel(ui->labelCircleCenter_5, 4, m_circleCenterOverlay);
    setRadiusLabel(ui->labelCircleRadius_5, 4, m_circleRadiusOverlay);

    // --- P6/R6 ---
    setCenterLabel(ui->labelCircleCenter_6, 5, m_circleCenterOverlay);
    setRadiusLabel(ui->labelCircleRadius_6, 5, m_circleRadiusOverlay);

    // --- P7/R7 ---
    setCenterLabel(ui->labelCircleCenter_7, 6, m_circleCenterOverlay);
    setRadiusLabel(ui->labelCircleRadius_7, 6, m_circleRadiusOverlay);

    // --- P8/R8 ---
    setCenterLabel(ui->labelCircleCenter_8, 7, m_circleCenterOverlay);
    setRadiusLabel(ui->labelCircleRadius_8, 7, m_circleRadiusOverlay);

    // --- P9/R9 ---
    setCenterLabel(ui->labelCircleCenter_9, 8, m_circleCenterOverlay);
    setRadiusLabel(ui->labelCircleRadius_9, 8, m_circleRadiusOverlay);
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
    m_circleCenterOverlay.clear();
    m_circleRadiusOverlay.clear();
    updateCircleIfoLables();

    // ===== 5. 九点标定 =====
    m_calibPoints.clear();
    updateCalibLabels();          // 把界面上显示 9 个世界坐标的 Label 清掉

    // 如果“清除痕迹”时也想顺便退出标定模式，可以加上这行：
    // m_isCalibrating = false;

    // ===== 6. 用当前原始帧重画一次（不带任何叠加） =====
    emit sigNewFrame(m_displayImage);
}




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

    // ============================================================
    // 【新增】 注册自定义类型，解决跨线程信号槽报错问题
    // ============================================================
    qRegisterMetaType<cv::Mat>("cv::Mat");
    qRegisterMetaType<QVector<QPointF>>("QVector<QPointF>");
    // ============================================================

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


    m_hasBoxOverlay = false;
    m_boxPointsOverlay.clear();

    // 保存图片相关缓存
    m_nSaveImageBufSize    = 0;
    m_pSaveImageBuf        = NULL;

    // 当前帧缓存（供 OpenCV 算法用）
    m_hasFrame             = false;
    m_frameBuffer.clear();
    // m_saveDir 默认空，第一次保存时弹出路径选择框

    //圆形检测
    m_hasCircleOverlay = false;
    m_circleCenterOverlay.clear();
    m_circleRadiusOverlay.clear();
    updateCircleIfoLables();
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

    // [新增] 动态测量初始化状态
    m_isDynamicMeasuring = false;
    m_homographyMat = cv::Mat(); // 空矩阵

    // 初始化 label 显示
    ui->labellength->setText("0.00");
    ui->labelwidth->setText("0.00");

    // --- [新增] 初始化后台测量线程 ---
    m_workerThread = new QThread(this);
    m_worker = new MeasureWorker(); // 不能指定 parent，否则不能 moveToThread
    m_worker->moveToThread(m_workerThread);

    // 连接信号槽
    // 主线程发图 -> 子线程处理
    connect(this, &MainWindow::startWorkerProcess, m_worker, &MeasureWorker::doProcess);

    // 子线程结果 -> 主线程更新 UI 数据
    connect(m_worker, &MeasureWorker::resultReady, this, [=](QVector<QPointF> pts, double len, double wid){
        // 收到新数据，只更新变量，不刷新界面（界面由 onNewFrame 的定时器或下一帧刷新）
        m_lastMeasurePts = pts;
        m_lastLen = len;
        m_lastWid = wid;

        // 实时更新文字 Label
        if (len > 0) {
            ui->labellength->setText(QString::number(len, 'f', 2));
            ui->labelwidth->setText(QString::number(wid, 'f', 2)); // 注意这里有个拼写错误，你变量名可能是 wid
            ui->labelwidth->setText(QString::number(wid, 'f', 2));
        }
    });

    // 线程结束自动清理
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_workerThread->start(); // 启动线程
}

MainWindow::~MainWindow()
{
    if (m_pcMyCamera)
    {
        m_pcMyCamera->Close();
        delete m_pcMyCamera;
        m_pcMyCamera = NULL;
    }
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
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
                       QImage::Format_RGB888);//============================================================================================================
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
 * @brief 槽函数：自动检测并采集 9 个标定圆心坐标
 * @details
 * * 目标：实现机器视觉应用中九点标定的“图像点自动获取”步骤。
 * * 流程概述：
 * 1. 线程安全地从相机缓存 (m_frameBuffer) 中获取当前图像帧。
 * 2. 根据相机的像素格式（Mono8, BGR8, RGB8）将图像转换为 OpenCV 的灰度图。
 * 3. 应用用户定义的 ROI 区域，缩小检测范围，并计算坐标偏移 (roiOffset)。
 * 4. 使用 OpenCV 的 cv::HoughCircles 函数在工作区域内进行圆检测。
 * 5. 严格检查检测到的圆数量是否达到 9 个。
 * 6. 对检测到的 9 个圆，使用自定义的 CircleResult 结构体进行排序，确保它们按 **“从上到下、从左到右”**（即 P1 到 P9）的顺序排列。
 * 7. 将 9 个圆心坐标（像素坐标）依次存储到核心标定容器 m_calibPoints 中。
 * 8. 实时刷新 UI 界面上对应的 18 个 QLabel 标签（labelCircleCenter/Radius 到 labelCircleCenter_9/Radius_9）。
 * 9. 设置 m_isCalibrating 标志，并触发图像重绘，在显示界面上绘制出这 9 个已采集的标定点。
 * * @note
 * - 函数使用了 QMutexLocker 确保数据在多线程环境下的安全性。
 * - 排序逻辑是此函数的关键，它保证了图像点 (P1~P9) 与机械臂世界坐标的正确对应关系。
 */
void MainWindow::on_bnFindCircle_clicked()
{
    // ===================================================
    // 1. 线程安全与图像帧缓存获取和转换 (保持不变)
    // ===================================================
    QMutexLocker locker(&m_frameMutex);
    if (!m_hasFrame)
    {
        QMessageBox::information(this, tr("提示"), tr("当前没有可处理的图像帧"));
        return;
    }

    int imgW = m_frameInfo.nWidth;
    int imgH = m_frameInfo.nHeight;
    cv::Mat gray;

    // ... [像素格式转换代码保持不变] ...
    if (m_frameInfo.enPixelType == PixelType_Gvsp_Mono8)
    {
        cv::Mat matGray(imgH, imgW, CV_8UC1, m_frameBuffer.data());
        gray = matGray.clone();
    }
    else if (m_frameInfo.enPixelType == PixelType_Gvsp_BGR8_Packed)
    {
        cv::Mat matBGR(imgH, imgW, CV_8UC3, m_frameBuffer.data());
        cv::cvtColor(matBGR, gray, cv::COLOR_BGR2GRAY);
    }
    else if (m_frameInfo.enPixelType == PixelType_Gvsp_RGB8_Packed)
    {
        cv::Mat matRGB(imgH, imgW, CV_8UC3, m_frameBuffer.data());
        cv::cvtColor(matRGB, gray, cv::COLOR_RGB2GRAY);
    }
    else
    {
        QMessageBox::warning(this, tr("提示"), tr("当前像素格式暂不支持圆检测"));
        return;
    }

    locker.unlock(); // 解锁

    if (gray.empty())
    {
        QMessageBox::warning(this, tr("提示"), tr("灰度图为空，无法检测"));
        return;
    }

    // ===================================================
    // 2. ROI 区域处理 (保持不变)
    // ===================================================
    cv::Mat workGray = gray;
    cv::Point2f roiOffset(0.0f, 0.0f);

    // ... [ROI 检查和设置代码保持不变] ...
    if (m_hasRoi)
    {
        QRectF rQt = currentRoiRect();
        int x = std::max(0, (int)std::floor(rQt.left()));
        int y = std::max(0, (int)std::floor(rQt.top()));
        int w = std::min(imgW - x, (int)std::ceil(rQt.width()));
        int h = std::min(imgH - y, (int)std::ceil(rQt.height()));

        if (w > 10 && h > 10)
        {
            cv::Rect roiRect(x, y, w, h);
            workGray = gray(roiRect).clone();
            roiOffset = cv::Point2f((float)x, (float)y);
        }
    }


    // ===================================================
    // 3. OpenCV 圆检测（HoughCircles）(保持不变)
    // ===================================================
    cv::GaussianBlur(workGray, workGray, cv::Size(9, 9), 2, 2);

    std::vector<cv::Vec3f> circles;
    cv::HoughCircles(workGray, circles, cv::HOUGH_GRADIENT, 1,
                     gray.rows / 8,
                     100, 30, 0, 0);


    // ===================================================
    // 4. 结果检查、提取和排序 (主要修改点)
    // ===================================================

    // **修改点 1：检查圆形数量**
    size_t detectedCount = circles.size();
    if (detectedCount == 0)
    {
        // 如果一个圆都没有检测到，直接返回并提示
        QMessageBox::information(this, tr("结果"), tr("未检测到任何圆形。"));
        m_hasCircleOverlay = false;
        m_circleCenterOverlay.clear();
        m_circleRadiusOverlay.clear();
        emit sigNewFrame(m_displayImage);
        return;
    }

    std::vector<CircleResult> results;
    for (size_t i = 0; i < detectedCount; ++i)
    {
        // ... [圆心坐标提取和 ROI 偏移代码保持不变] ...
        float cx = circles[i][0] + roiOffset.x;
        float cy = circles[i][1] + roiOffset.y;
        float r  = circles[i][2];
        results.push_back({{cx, cy}, r});
    }

    // 无论数量多少，都对所有检测到的圆进行排序
    std::sort(results.begin(), results.end());

    // ===================================================
    // 5. UI 更新与数据存储 (主要修改点)
    // ===================================================

    const int MAX_LABELS = 9; // 最大处理数量

    // UI 设计器中创建了 9 对标签 (保持不变)
    QLabel* centerLabels[] = {
        ui->labelCircleCenter, ui->labelCircleCenter_2, ui->labelCircleCenter_3,
        ui->labelCircleCenter_4, ui->labelCircleCenter_5, ui->labelCircleCenter_6,
        ui->labelCircleCenter_7, ui->labelCircleCenter_8, ui->labelCircleCenter_9
    };
    QLabel* radiusLabels[] = {
        ui->labelCircleRadius, ui->labelCircleRadius_2, ui->labelCircleRadius_3,
        ui->labelCircleRadius_4, ui->labelCircleRadius_5, ui->labelCircleRadius_6,
        ui->labelCircleRadius_7, ui->labelCircleRadius_8, ui->labelCircleRadius_9
    };

    // 清空旧数据
    m_circleCenterOverlay.clear();
    m_circleRadiusOverlay.clear();

    QString infoText;
    int totalDetectedCount = (int)results.size(); // 实际检测到的总数

    // **修改点 2：检查是否超过最大显示数量并发出警告**
    if (totalDetectedCount > MAX_LABELS)
    {
        QMessageBox::warning(this, tr("警告"),
                             tr("检测到的圆形数量过多（找到 %1 个，最大显示 %2 个）。程序将只处理并显示前 %2 个圆形。")
                                 .arg(totalDetectedCount)
                                 .arg(MAX_LABELS));
    }

    // 确定实际要处理和绘制的数量（限制在 9 个以内）
    int processCount = std::min(totalDetectedCount, MAX_LABELS);

    // **修改点 3：循环处理检测到的圆形 (限制到 MAX_LABELS)**
    for (int i = 0; i < processCount; ++i)
    {
        const CircleResult& current = results[i];

        // 1. 更新 UI 标签
        QString centerStr = QString("(%1, %2)").arg(current.center.x, 0, 'f', 2).arg(current.center.y, 0, 'f', 2);
        QString radiusStr = QString::number(current.radius, 'f', 2);

        centerLabels[i]->setText(centerStr);
        radiusLabels[i]->setText(radiusStr);

        // 2. 存储到标定点容器
        m_circleCenterOverlay.append(current.center);
        m_circleRadiusOverlay.append(current.radius);

        // 3. 构造信息反馈
        infoText += QString("P%1: %2, R: %3\n").arg(i + 1).arg(centerStr).arg(radiusStr);
    }

    // **修改点 4：清空剩余的标签，显示“未知”或“---”**
    for (int i = processCount; i < MAX_LABELS; ++i) {
        centerLabels[i]->setText(tr("未知")); // 保持原代码的 '未知'
        radiusLabels[i]->setText(tr("---"));
    }

    // 设置找圆心模式标志
    m_hasCircleOverlay = (processCount > 0);

    // **修改点 5：修改最终信息提示**
    if (totalDetectedCount > 0) {
        QMessageBox::information(this, tr("结果"),
                                 tr("已检测到 %1 个圆形，处理并显示了其中 %2 个。")
                                    .arg(totalDetectedCount)
                                    .arg(processCount) + "\n\n" + infoText);
    } else {
        // 由于在第 4 节开头已经处理了 totalDetectedCount == 0 的情况并返回，
        // 理论上这里不会执行，但为了完整性保留。
        QMessageBox::information(this, tr("结果"), tr("未检测到任何圆形。"));
    }

    // 通知界面重绘，绘制 m_circleCenterOverlay和m_circleRadiusOverlay 中的点
    emit sigNewFrame(m_displayImage);
}


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

    // 1. 获取灰度图 (深拷贝)
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

    // =====================================================
    // 1.5 使用 ROI：如果有有效 ROI，则只在 ROI 范围内找盒子
    // =====================================================
    cv::Mat workGray;
    cv::Rect roiRect;
    cv::Point2f roiOffset(0.f, 0.f);

    if (m_hasRoi)
    {
        QRectF rQt = currentRoiRect();
        // clamp 到图像范围
        int x = std::max(0, (int)std::floor(rQt.left()));
        int y = std::max(0, (int)std::floor(rQt.top()));
        int w = std::min(imgW - x, (int)std::ceil(rQt.width()));
        int h = std::min(imgH - y, (int)std::ceil(rQt.height()));

        if (w > 10 && h > 10)  // 太小的 ROI 就当无效
        {
            roiRect   = cv::Rect(x, y, w, h);
            workGray  = gray(roiRect).clone();
            roiOffset = cv::Point2f((float)x, (float)y);
        }
    }

    // 如果没有有效 ROI，就在整幅图上处理
    if (workGray.empty())
        workGray = gray;

    // 2. 预处理（在 ROI 或整图上做）
    cv::Mat blurred;
    cv::GaussianBlur(workGray, blurred, cv::Size(9, 9), 2, 2); // 加大模糊，平滑毛刺

    // 3. 智能二值化（基于 ROI/整图的亮度）
    cv::Scalar meanVal = cv::mean(blurred);
    int threshType = cv::THRESH_BINARY | cv::THRESH_OTSU;
    if (meanVal[0] > 100) {
        // 白底黑物 -> 反转
        threshType = cv::THRESH_BINARY_INV | cv::THRESH_OTSU;
    }
    cv::Mat bin;
    cv::threshold(blurred, bin, 0, 255, threshType);

    // 4. 关键：重度防粘连 (Heavy Duty Separation)
    cv::Mat kernelHeavy   = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(13, 13));
    cv::Mat kernelRestore = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(15, 15));

    // 腐蚀：让物体大幅“变瘦”，从而断开连接
    cv::erode(bin, bin, kernelHeavy, cv::Point(-1,-1), 2);
    // 膨胀：让物体“复原”，保持轮廓位置正确
    cv::dilate(bin, bin, kernelRestore, cv::Point(-1,-1), 2);

    // 5. 查找轮廓（在 ROI/整图的 bin 上）
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    std::vector<cv::Point2f> allRectPoints; // 存所有（整图坐标）
    std::vector<cv::Point2f> maxRectPoints; // 存最大的（整图坐标）
    double maxScore = 0;

    for (const auto &cnt : contours)
    {
        double area = cv::contourArea(cnt);
        if (area < 1000) continue; // 忽略过小的碎片

        cv::RotatedRect rRect = cv::minAreaRect(cnt);
        double rectArea = rRect.size.width * rRect.size.height;
        if (rectArea <= 1.0) continue;

        // 矩形度筛选
        double solidity = area / rectArea;
        if (solidity < 0.6) continue;   // 腐蚀后形状可能会变一点，稍微放宽

        double score = area * solidity;

        // 拟合
        std::vector<cv::Point> approx;
        cv::approxPolyDP(cnt, approx, 0.04 * cv::arcLength(cnt, true), true);

        std::vector<cv::Point2f> currentQuad;   // 用于保存“整图坐标系”的四个点

        if (approx.size() == 4 && cv::isContourConvex(approx)) {
            for (auto p : approx)
            {
                // 轮廓点坐标 + ROI 偏移 = 回到整图坐标系
                currentQuad.push_back(cv::Point2f(
                                          p.x + roiOffset.x,
                                          p.y + roiOffset.y));
            }
        } else {
            // 拟合失败用外接矩形兜底
            cv::Point2f pts[4];
            rRect.points(pts);
            for (int i = 0; i < 4; i++)
            {
                currentQuad.push_back(cv::Point2f(
                                          pts[i].x + roiOffset.x,
                                          pts[i].y + roiOffset.y));
            }
        }

        // 亚像素优化（在整图 gray 上优化整图坐标的角点）
        bool inBound = true;
        for (const auto& p : currentQuad) {
            if (p.x < 5 || p.y < 5 || p.x > imgW - 5 || p.y > imgH - 5)
                inBound = false;
        }
        if (inBound) {
            cv::TermCriteria criteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 20, 0.01);
            cv::cornerSubPix(gray, currentQuad, cv::Size(9, 9), cv::Size(-1, -1), criteria);
        }

        // 按 P1~P4 顺序排序
        sortRectPoints(currentQuad);

        // 存入列表（全是整图坐标）
        allRectPoints.insert(allRectPoints.end(), currentQuad.begin(), currentQuad.end());

        // 更新最大值
        if (score > maxScore) {
            maxScore = score;
            maxRectPoints = currentQuad;
        }
    }

    if (!allRectPoints.empty())
    {
        // 更新 UI：显示选中(最大)矩形的坐标
        if (maxRectPoints.size() == 4) {
            ui->labelBoxP1->setText(QString("(%1, %2)").arg(maxRectPoints[0].x, 0, 'f', 1).arg(maxRectPoints[0].y, 0, 'f', 1));
            ui->labelBoxP2->setText(QString("(%1, %2)").arg(maxRectPoints[1].x, 0, 'f', 1).arg(maxRectPoints[1].y, 0, 'f', 1));
            ui->labelBoxP3->setText(QString("(%1, %2)").arg(maxRectPoints[2].x, 0, 'f', 1).arg(maxRectPoints[2].y, 0, 'f', 1));
            ui->labelBoxP4->setText(QString("(%1, %2)").arg(maxRectPoints[3].x, 0, 'f', 1).arg(maxRectPoints[3].y, 0, 'f', 1));
        }

        {
            QMutexLocker locker2(&m_frameMutex);
            // 红框：最大的 (用于 UI 交互和畸变矫正)
            m_lastQuad = maxRectPoints;
            m_hasQuad  = true;

            // 黄框：所有的 (包含最大的和较小的)
            m_boxPointsOverlay.clear();
            for (const auto& p : allRectPoints) {
                m_boxPointsOverlay.push_back(p);
            }
            m_hasBoxOverlay = true;
        }

        emit sigNewFrame(m_displayImage);
    }
    else
    {
        QMessageBox::information(this, "提示", "未检测到有效矩形");
    }
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
        cv::cvtColor(gray, src, cv::COLOR_GRAY2RGB);//=============================================================
    }
    else if (m_frameInfo.enPixelType == PixelType_Gvsp_RGB8_Packed)//=============================================
    {
        src = cv::Mat(imgH, imgW, CV_8UC3, m_frameBuffer.data()).clone();
    }
    else if (m_frameInfo.enPixelType == PixelType_Gvsp_BGR8_Packed)//===============================================
    {
        cv::Mat rgb(imgH, imgW, CV_8UC3, m_frameBuffer.data());
        cv::cvtColor(rgb, src, cv::COLOR_BGR2RGB);//===================================================================
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
                QImage::Format_RGB888);//========================================================================================
    QPixmap pix = QPixmap::fromImage(qimg.copy());

    QLabel *dlg = new QLabel;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("畸变矫正结果"));
    dlg->setPixmap(pix);
    dlg->resize(pix.size());
    dlg->show();
}



/**
 * @brief 导入世界坐标点辅助函数
 * @details 弹出文件对话框，读取用户选择的 CSV/TXT 文件，并解析出九个世界坐标点。
 * 该函数兼容 'P#, X, Y' 格式，并自动跳过第一行表头。
 * @return 导入成功返回 true，否则返回 false。
 */
bool MainWindow::importWorldCoordinates()
{
    const int CALIB_POINTS_COUNT = 9;

    // 1. 弹出文件选择对话框
    QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("选择世界坐标文件"),
        QDir::homePath(),
        tr("数据文件 (*.csv *.txt)")
    );

    if (filePath.isEmpty())
    {
        QMessageBox::information(this, tr("取消操作"), tr("您取消了文件选择。"));
        return false;
    }

    // 2. 读取文件内容
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::critical(this, tr("文件错误"),
                              tr("无法打开文件进行读取：\n路径：%1\n错误：%2\n请检查文件权限。")
                              .arg(filePath).arg(file.errorString()));
        return false;
    }

    QTextStream in(&file);

    // 【关键修复】明确指定文件编码为 UTF-8，以正确处理中文注释和数字
    in.setCodec("UTF-8");

    QVector<cv::Point2f> tempPoints;
    // 使用 QString::SkipEmptyParts 确保 Qt 兼容性
    const QString::SplitBehavior behavior = QString::SkipEmptyParts;

    // 3. 解析文件内容
    // 用于跳过文件中的第一行表头。
    bool isFirstLine = true;

    while (!in.atEnd())
    {
        QString line = in.readLine().trimmed();

        // 确保跳过空行和注释行 (#)
        if (line.isEmpty() || line.startsWith('#')) continue;

        // 【关键修复】跳过第一行，因为它通常是表头（即使是乱码）
        if (isFirstLine) {
             qDebug() << "Skipping header line:" << line;
             isFirstLine = false;
             continue;
        }

        // 尝试使用逗号、分号或空格作为分隔符
        QStringList parts;
        if (line.contains(',')) {
            parts = line.split(',', behavior);
        } else if (line.contains(';')) {
            parts = line.split(';', behavior);
        } else {
            // 默认按空格或制表符分隔
            parts = line.split(QRegularExpression("[\\s]+"), behavior);
        }

        // 检查是否有足够的列（P# + X + Y = 至少 3 列）
        if (parts.size() >= 3)
        {
            bool xOk = false;
            bool yOk = false;

            // 【核心修正】从索引 1 读取 X，索引 2 读取 Y，跳过索引 0 (P# 序号)
            float x = parts.at(1).toFloat(&xOk); // X坐标 (第2列)
            float y = parts.at(2).toFloat(&yOk); // Y坐标 (第3列)

            if (xOk && yOk)
            {
                tempPoints.append(cv::Point2f(x, y));
                qDebug() << "Successfully parsed P" << tempPoints.size() << ": (" << x << "," << y << ")";
            } else {
                qDebug() << "Failed to convert coordinates to float in line:" << line
                         << ". X-Value:" << parts.at(1) << "Y-Value:" << parts.at(2);
            }
        } else {
            qDebug() << "Skipping line due to insufficient columns or invalid data:" << line;
        }

        // 如果点数已够，停止读取
        if (tempPoints.size() == CALIB_POINTS_COUNT) break;
    }

    file.close();

    // 4. 验证点数
    if (tempPoints.size() != CALIB_POINTS_COUNT)
    {
        QMessageBox::critical(this, tr("数据错误"),
                              tr("文件解析到的坐标点数量不足。需要 %1 个点，但只找到 %2 个点。\n请确保文件中只有 %1 行有效数据，格式为 'P#, X, Y'。")
                              .arg(CALIB_POINTS_COUNT).arg(tempPoints.size()));
        return false;
    }

    // 5. 导入成功
    m_calibPoints = tempPoints;
    updateCalibLabels();
    QMessageBox::information(this, tr("导入成功"),
                             tr("已成功从文件导入 %1 组世界坐标。").arg(CALIB_POINTS_COUNT));
    return true;
}
/**
 * @brief 递归/状态控制函数：提示用户输入世界坐标 (核心逻辑)
 * @param index 当前需要输入的标定点序号 (0-8)
 */
void MainWindow::promptForWorldCoordinate(int index)
{
    const int CALIB_POINTS_COUNT = 9;

    // ----------------------------------------
    // 1. 退出条件和完成处理
    // ----------------------------------------
    if (index >= CALIB_POINTS_COUNT)
    {
        QMessageBox::information(this, tr("标定准备完成"),
                                 tr("已成功采集并存储 9 组世界坐标。即将开始标定计算。"));
        m_isCalibrating = true;
        // 【下一步】调用您的标定计算函数
        // calculateHomography(m_circleCenterOverlay, m_calibPoints);
        return;
    }

    // ----------------------------------------
    // 2. 提示用户输入
    // ----------------------------------------
    QString title = tr("输入世界坐标");
    QString label = tr("请输入点 P%1 的世界坐标 (X, Y)，格式如： 10.0, 25.5").arg(index + 1);

    // 预填充当前值（如果是回退修改）
    QString initialText = "";
    if (index < m_calibPoints.size()) {
        const cv::Point2f& p = m_calibPoints[index];
        initialText = QString("%1, %2").arg(p.x, 0, 'f', 4).arg(p.y, 0, 'f', 4);
    }

    bool ok;
    QString text = QInputDialog::getText(this, title, label, QLineEdit::Normal, initialText, &ok);

    // ----------------------------------------
    // 3. 用户取消或输入为空
    // ----------------------------------------
    if (!ok || text.isEmpty())
    {
        // P1 取消直接退出
        if (index == 0) {
            QMessageBox::information(this, tr("标定中断"), tr("标定流程已取消。"));
            m_calibPoints.clear();
            updateCalibLabels();
            m_isCalibrating = false;
            return;
        }

        // P2-P9 取消，提供回退选项
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, tr("输入被取消"),
                                      tr("P%1 坐标输入无效或已取消。您希望：\n\n[Yes] 返回上一个点 P%2 进行修改。\n[No] 彻底取消标定流程。")
                                      .arg(index + 1)
                                      .arg(index),
                                      QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel); // Cancel 没用但习惯性添加

        if (reply == QMessageBox::Yes)
        {
            // 返回上一个点
            m_calibPoints.removeLast(); // 移除上一个点（因为它现在会重新输入）
            updateCalibLabels();
            promptForWorldCoordinate(index - 1); // 递归调用上一个点
            return;
        }
        else // QMessageBox::No 或其他（如 Esc）
        {
            QMessageBox::information(this, tr("标定中断"), tr("标定流程已取消。"));
            m_calibPoints.clear();
            updateCalibLabels();
            m_isCalibrating = false;
            return;
        }
    }

    // ----------------------------------------
    // 4. 解析和验证输入
    // ----------------------------------------
    int commaIndex = text.indexOf(',');
    if (commaIndex > 0)
    {
        QString xStr = text.mid(0, commaIndex).trimmed();
        QString yStr = text.mid(commaIndex + 1).trimmed();

        bool xOk = false;
        bool yOk = false;

        float x = xStr.toFloat(&xOk);
        float y = yStr.toFloat(&yOk);

        if (xOk && yOk)
        {
            // ----------------------------------------
            // 5. 存储成功并继续下一个点
            // ----------------------------------------
            if (index < m_calibPoints.size()) {
                // 如果是回退修改，则替换当前点
                m_calibPoints[index] = cv::Point2f(x, y);
            } else {
                // 否则，追加新点
                m_calibPoints.append(cv::Point2f(x, y));
            }

            updateCalibLabels();
            promptForWorldCoordinate(index + 1); // 递归调用下一个点
            return;
        }
    }

    // ----------------------------------------
    // 6. 格式错误处理
    // ----------------------------------------
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, tr("格式错误"),
                                  tr("坐标格式不正确 (必须是 X.X, Y.Y)，请问您希望：\n\n[Yes] 重新输入点 P%1。\n[No] 返回上一个点 P%2 进行修改。")
                                  .arg(index + 1)
                                  .arg(index == 0 ? 1 : index), // 如果是 P1，显示 P1，否则显示 Pn-1
                                  QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes)
    {
        // 重新输入当前点
        promptForWorldCoordinate(index); // 递归调用当前点
        return;
    }
    else if (reply == QMessageBox::No)
    {
        if (index == 0) {
            // P1 处选择返回上一个点（即取消）
            QMessageBox::information(this, tr("标定中断"), tr("标定流程已取消。"));
            m_calibPoints.clear();
            updateCalibLabels();
            m_isCalibrating = false;
            return;
        }
        // 返回上一个点
        m_calibPoints.removeLast(); // 移除上一个点
        updateCalibLabels();
        promptForWorldCoordinate(index - 1); // 递归调用上一个点
        return;
    }
}
/**
 * @brief “开始九点标定” 按钮
 * @details
 *   - 清空已有标定点
 *   - 等待用户输入9个世界坐标
 */
const int CALIB_POINTS_COUNT = 9;

void MainWindow::on_bnStartCalib9_clicked()
{
    // 1. 检查像素坐标是否已准备好（九个圆心）
    if (m_circleCenterOverlay.size() != CALIB_POINTS_COUNT)
    {
        QMessageBox::warning(this, tr("警告"),
                             tr("请先运行圆检测，并确保成功检测到 %1 个像素点（圆心）。").arg(CALIB_POINTS_COUNT));
        return;
    }

    // 2. 清空旧数据，准备开始
    m_calibPoints.clear();
    updateCalibLabels(); // 清空界面显示

    // 3. 弹出选择对话框，询问用户输入方式
    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("选择坐标输入方式"));
    msgBox.setText(tr("请选择九个世界坐标的输入方式:"));

    // 创建两个按钮，使用 AcceptRole 和 RejectRole 等标准角色或直接使用 Accept/NoRole
    // 修复：使用 AcceptRole 和 NoRole 替代 ActionRole
    QPushButton *manualButton = msgBox.addButton(tr("手动输入 (逐个)"), QMessageBox::AcceptRole);
    QPushButton *importButton = msgBox.addButton(tr("导入文件 (CSV/TXT)"), QMessageBox::NoRole);
    msgBox.addButton(tr("取消"), QMessageBox::RejectRole); // 取消按钮

    // 设置默认按钮（可选）
    msgBox.setDefaultButton(manualButton);

    msgBox.exec();

    // 4. 根据用户选择执行相应操作
    if (msgBox.clickedButton() == manualButton)
    {
        // 启动手动输入流程 (递归弹窗)
        promptForWorldCoordinate(0);
    }
    else if (msgBox.clickedButton() == importButton)
    {
        // 启动文件导入流程
        bool success = importWorldCoordinates();
        if (success)
        {
            QMessageBox::information(this, tr("导入成功"),
                                     tr("已成功从文件导入 %1 组世界坐标。").arg(CALIB_POINTS_COUNT));
        }
        else
        {
            // importWorldCoordinates 内部会显示错误信息
            m_calibPoints.clear();
            updateCalibLabels();
        }
    }
    else
    {
        // 用户选择了取消或关闭对话框
        return;
    }
}


/**
 * @brief 处理 “开始计算转换矩阵” 按钮 (bnStartCalculate) 的点击事件
 * @details 检查 9 个像素点和 9 个世界坐标是否完整，然后计算单应性矩阵 H
 */
void MainWindow::on_bnStartCalculate_clicked()
{
    const int CALIB_POINTS_COUNT = 9;

    // ----------------------------------------
    // 1. 检查数据完备性
    // ----------------------------------------
    if (m_circleCenterOverlay.size() != CALIB_POINTS_COUNT)
    {
        QMessageBox::warning(this, tr("计算错误"),
                             tr("像素坐标数据不足。需要 %1 个圆心坐标，当前只有 %2 个。")
                             .arg(CALIB_POINTS_COUNT).arg(m_circleCenterOverlay.size()));
        return;
    }

    if (m_calibPoints.size() != CALIB_POINTS_COUNT)
    {
        QMessageBox::warning(this, tr("计算错误"),
                             tr("世界坐标数据不足。需要 %1 组世界坐标，当前只有 %2 组。\n请先完成世界坐标输入。")
                             .arg(CALIB_POINTS_COUNT).arg(m_calibPoints.size()));
        return;
    }

    // ----------------------------------------
    // 2. 转换数据格式为 OpenCV 期望的 std::vector
    // ----------------------------------------
    // m_circleCenterOverlay 是像素点 (Image Points, 源点)
    // m_calibPoints 是世界点 (World Points, 目标点)
    std::vector<Point2f> imagePoints(m_circleCenterOverlay.begin(), m_circleCenterOverlay.end());
    std::vector<Point2f> worldPoints(m_calibPoints.begin(), m_calibPoints.end());

    // ----------------------------------------
    // 3. 计算单应性矩阵 (Homography Matrix)
    //    H * [u, v, 1]^T = lambda * [X, Y, 1]^T
    //    这里的 H 是将图像坐标 (u, v) 转换到世界坐标 (X, Y) 的矩阵
    //    使用 RANSAC 方法可以增强鲁棒性，但九点标定常用简单线性方法 (0)
    // ----------------------------------------
    try
    {
        // 目标： H 将 图像点 -> 世界点
        // opencv findHomography(srcPoints, dstPoints, method, RANSAC_Threshold)
        // srcPoints = imagePoints (像素点)
        // dstPoints = worldPoints (世界点)
        m_H = findHomography(imagePoints, worldPoints, 0); // 使用 0 (CV_LMEDS/CV_RANSAC) 可能会更好，但这里默认使用 0，即 LMEDS 或 RANSAC 的默认选择。

        if (m_H.empty() || m_H.cols != 3 || m_H.rows != 3)
        {
            QMessageBox::critical(this, tr("计算失败"), tr("计算单应性矩阵失败，请检查输入数据是否存在共线等问题。"));
            m_hasH = false;
            return;
        }

        // ----------------------------------------
        // 4. 存储结果并提示成功
        // ----------------------------------------
        m_hasH = true;

        // 可选：将 H 矩阵打印到控制台或日志
        qDebug() << "Homography Matrix H (Image to World):\n"
                 << m_H.at<double>(0, 0) << m_H.at<double>(0, 1) << m_H.at<double>(0, 2) << "\n"
                 << m_H.at<double>(1, 0) << m_H.at<double>(1, 1) << m_H.at<double>(1, 2) << "\n"
                 << m_H.at<double>(2, 0) << m_H.at<double>(2, 1) << m_H.at<double>(2, 2);

        QMessageBox::information(this, tr("计算成功"),
                                 tr("单应性转换矩阵计算成功！\n现在可以使用该矩阵进行坐标转换。"));

        // 标定完成后，可以考虑将 m_isCalibrating 设为 false，防止继续输入
        m_isCalibrating = false;

    }
    catch (const cv::Exception& e)
    {
        QMessageBox::critical(this, tr("OpenCV 错误"), tr("计算单应性矩阵时发生 OpenCV 异常: %1").arg(e.what()));
        m_hasH = false;
        return;
    }
}

/**
 * @brief 处理 “保存标定结果” 按钮的点击事件
 * @details 将计算得到的单应性矩阵 m_H 保存到用户选择的 .txt 或 .csv 文件中。
 */
void MainWindow::on_bnSaveCalib9_clicked()
{
    // 1. 检查标定结果是否已经计算
    if (!m_hasH || m_H.empty() || m_H.cols != 3 || m_H.rows != 3)
    {
        QMessageBox::warning(this, tr("保存失败"), tr("单应性矩阵尚未计算或计算结果无效，请先执行计算步骤。"));
        return;
    }

    // 2. 弹出文件保存对话框，允许用户选择 .txt 或 .csv
    QString defaultFileName = QString("H_Matrix_%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString filter = tr("文本文件 (*.txt);;Excel CSV 文件 (*.csv)");

    QString savePath = QFileDialog::getSaveFileName(
        this,
        tr("保存单应性矩阵"),
        defaultFileName,
        filter
    );

    // 检查用户是否取消了保存操作
    if (savePath.isEmpty())
    {
        qDebug() << "Save operation cancelled by user.";
        return;
    }

    // 3. 确定分隔符和文件格式
    QFile file(savePath);
    QString separator = " "; // 默认用于 .txt

    // 检查文件扩展名以确定格式
    if (savePath.toLower().endsWith(".csv"))
    {
        separator = ","; // CSV 格式使用逗号分隔
    }
    // 注意：如果用户没有输入扩展名，QFileDialog 会自动添加过滤器中的第一个 (*.txt)

    // 4. 准备写入文件
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::critical(this, tr("文件写入失败"), tr("无法打开文件进行写入: %1").arg(file.errorString()));
        return;
    }

    QTextStream out(&file);

    // 设置浮点数精度，确保数值完整
    out.setRealNumberPrecision(15);

    // 5. 写入矩阵头部信息
    out << "# Homography Matrix H (Image to World) - 3x3\n";
    out << "# Format: Row-major, separated by '" << separator << "'\n";
    out << "# Generated on: " << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "\n";

    // 6. 写入 3x3 矩阵数据
    // 矩阵数据按行写入，适用于 CSV (逗号分隔) 和 TXT (空格分隔)
    for (int i = 0; i < m_H.rows; ++i)
    {
        QString rowData;
        for (int j = 0; j < m_H.cols; ++j)
        {
            // 获取矩阵元素 (double 类型)
            rowData += QString::number(m_H.at<double>(i, j));

            // 元素后添加分隔符，最后一个元素除外
            if (j < m_H.cols - 1)
            {
                rowData += separator;
            }
        }
        out << rowData << "\n";
    }

    file.close();

    // 7. 提示用户保存成功
    QMessageBox::information(this, tr("保存成功"), tr("单应性矩阵已成功保存到:\n%1").arg(savePath));
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
/*
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
*/

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
// ------------------------------------------------------------------------------------------------
// [合并版] onNewFrame: 包含动态测量 + 原有的所有绘图逻辑
// ------------------------------------------------------------------------------------------------
void MainWindow::onNewFrame(const QImage &img)
{
    if (img.isNull()) return;

    // 1. 【极速显示】直接把原图显示出来，不要等待算法！
    m_displayImage = img.copy(); // 浅拷贝或深拷贝均可，QImage 隐式共享很快
    QPixmap pix = QPixmap::fromImage(m_displayImage);

    // 2. 【后台计算】准备一张小图发给后台线程
    // 只有在开启测量且矩阵有效时才发
    if (m_isDynamicMeasuring && !m_homographyMat.empty())
    {
        // 降采样倍率，0.2 表示缩小5倍
        double scale = 1;

        // 我们需要把 QImage 转成 cv::Mat 发给子线程
        // 这里为了极致速度，先在 UI 线程 resize 成小图 (QImage::scaled 很快)
        // 比如 2500x2000 -> 500x400，数据量极小
        QImage smallImg = img.scaled(img.width() * scale, img.height() * scale, Qt::KeepAspectRatio, Qt::FastTransformation);

        // 转成 Mat (注意格式)
        cv::Mat smallMat;
        if (smallImg.format() == QImage::Format_Grayscale8) {
            smallMat = cv::Mat(smallImg.height(), smallImg.width(), CV_8UC1, (void*)smallImg.constBits(), smallImg.bytesPerLine()).clone();
        } else {
            // 假设 RGB
            cv::Mat temp(smallImg.height(), smallImg.width(), CV_8UC3, (void*)smallImg.constBits(), smallImg.bytesPerLine());
            // 必须 clone (深拷贝)，因为 smallImg 在函数结束后会销毁，子线程需要独立的数据
            smallMat = temp.clone();
        }

        // 发送信号给子线程！这步是瞬间完成的，不会阻塞
        emit startWorkerProcess(smallMat, scale);
    }

//3.绘制结果
    if (!pix.isNull())
    {
        QPainter painter(&pix);
        painter.setRenderHint(QPainter::Antialiasing, true);
        // 如果有测量数据，就画出来
        if (m_isDynamicMeasuring && !m_lastMeasurePts.isEmpty())
        {
            // 画绿框
            QPen pen(Qt::green);
            pen.setWidth(4);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            QPolygonF poly(m_lastMeasurePts);
            painter.drawPolygon(poly);

            // 画红字 (位置在第一个点旁边)
            // 计算矩形几何中心
            QPointF center(0, 0);
            for(const auto& pt : m_lastMeasurePts) center += pt;
            if(m_lastMeasurePts.size() > 0) center /= m_lastMeasurePts.size();

            QPen penText(Qt::red);
            painter.setPen(penText);

            QFont font = painter.font();
            font.setPixelSize(72);

            font.setBold(true);
            painter.setFont(font);

            QString text = QString("L:%1 W:%2").arg(m_lastLen, 0, 'f', 1).arg(m_lastWid, 0, 'f', 1);
            // 计算文字宽度，为了让文字精确居中
            QFontMetrics fm(font);
            int textWidth = fm.width(text);
            int textHeight = fm.height();

            // 在中心点绘制 (减去一半宽高以居中)
            painter.drawText(center.x() - textWidth / 2, center.y() + textHeight / 4, text);
            // ---------------------------------------------------------
        }

        // ==========================================
        // [原有功能] 交互式测距 (Ruler)
        // ==========================================
        if (m_isMeasuring)
        {
            QPen penMeasure(Qt::cyan);
            penMeasure.setWidth(2);
            penMeasure.setStyle(Qt::DashLine);
            painter.setPen(penMeasure);

            QPointF p1(m_measureStart.x, m_measureStart.y);
            QPointF p2(m_measureEnd.x, m_measureEnd.y);
            painter.drawLine(p1, p2);
            painter.setBrush(Qt::cyan);
            painter.drawEllipse(p1, 4, 4);
            painter.drawEllipse(p2, 4, 4);

            float dx = m_measureStart.x - m_measureEnd.x;
            float dy = m_measureStart.y - m_measureEnd.y;
            float dist = std::sqrt(dx*dx + dy*dy);

            QPointF midPoint = (p1 + p2) / 2;
            painter.setPen(Qt::yellow);
            QFont font = painter.font();
            font.setPixelSize(24);
            font.setBold(true);
            painter.setFont(font);
            painter.drawText(midPoint + QPointF(10, -10), QString("Dist: %1 px").arg(dist, 0, 'f', 1));
        }

        // ==========================================
        // [原有功能] 画 ROI 框
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
                QColor fill(0, 255, 0, 40);
                painter.setBrush(fill);
                painter.drawRect(r);
            }
        }

        // ==========================================
        // [原有功能] 画所有检测到的矩形 (黄色细线)
        // ==========================================
        if (m_hasBoxOverlay && !m_boxPointsOverlay.isEmpty())
        {
            QPen penAll(Qt::yellow);
            penAll.setWidth(1);
            painter.setPen(penAll);
            painter.setBrush(Qt::NoBrush);

            for (int i = 0; i < m_boxPointsOverlay.size(); i += 4)
            {
                if (i + 3 >= m_boxPointsOverlay.size()) break;
                QPolygonF poly;
                poly << QPointF(m_boxPointsOverlay[i].x, m_boxPointsOverlay[i].y)
                     << QPointF(m_boxPointsOverlay[i+1].x, m_boxPointsOverlay[i+1].y)
                     << QPointF(m_boxPointsOverlay[i+2].x, m_boxPointsOverlay[i+2].y)
                     << QPointF(m_boxPointsOverlay[i+3].x, m_boxPointsOverlay[i+3].y);
                painter.drawPolygon(poly);
            }
        }

        // ==========================================
        // [原有功能] 画当前选中的最大矩形 (红色粗线)
        // ==========================================
        if (m_hasQuad && m_lastQuad.size() == 4)
        {
            QPen penSelected(Qt::red);
            penSelected.setWidth(3);
            painter.setPen(penSelected);
            QPolygonF poly;
            for(const auto& p : m_lastQuad) poly << QPointF(p.x, p.y);
            painter.drawPolygon(poly);

            // 标出 P1~P4
            painter.setPen(Qt::green);
            QFont font = painter.font();
            font.setPixelSize(16);
            painter.setFont(font);
            painter.drawText(QPointF(m_lastQuad[0].x, m_lastQuad[0].y), "P1");
            painter.drawText(QPointF(m_lastQuad[1].x, m_lastQuad[1].y), "P2");
            painter.drawText(QPointF(m_lastQuad[2].x, m_lastQuad[2].y), "P3");
            painter.drawText(QPointF(m_lastQuad[3].x, m_lastQuad[3].y), "P4");
        }

        // ==========================================
        // [原有功能] 其他绘制 (圆、标定点、FPS)
        // ==========================================
        // 修改为支持 QVector (多圆绘制)
        if (m_hasCircleOverlay && !m_circleRadiusOverlay.isEmpty()) {
             QPen penCircle(Qt::red);
             penCircle.setWidth(3);
             painter.setPen(penCircle);

             // 遍历所有圆进行绘制
             for (int i = 0; i < m_circleRadiusOverlay.size(); ++i) {
                 // 防止索引越界（确保中心点数组也有对应数据）
                 if (i >= m_circleCenterOverlay.size()) break;

                 float r = m_circleRadiusOverlay[i];

                 // 如果半径有效，则绘制
                 if (r > 0.0f) {
                     // 从 QVector 中取出第 i 个点
                     cv::Point2f center = m_circleCenterOverlay[i];
                     QPointF c(center.x, center.y);
                     painter.drawEllipse(c, r, r);
                 }
             }
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
        int step = 4; // 采样步长优化性能
        for (int y = 0; y < grayImg.height(); y+=step) {
            const uchar *line = grayImg.constScanLine(y);
            for (int x = 0; x < grayImg.width(); x+=step) sum += line[x];
        }
        double meanGray = sum / double((grayImg.width()/step) * (grayImg.height()/step));
        ui->labelMeanGray->setText(QString::number(meanGray, 'f', 1));
/*
        // 中心十字准心
        double cx = pix.width()  / 2.0;
        double cy = pix.height() / 2.0;
        QPen penCross(Qt::green);
        penCross.setWidth(1);
        penCross.setStyle(Qt::DashLine);
        painter.setPen(penCross);
        painter.drawLine(QPointF(cx, 0), QPointF(cx, pix.height()));
        painter.drawLine(QPointF(0, cy), QPointF(pix.width(), cy));
*/
    }

    m_pixItem->setPixmap(pix);
    m_scene->setSceneRect(pix.rect());
}
//【新增】
// ---------------------------------------------------------
// 2. 实现按钮点击槽函数
// ---------------------------------------------------------
void MainWindow::on_bnDynamicMeasure_clicked()
{
    // 如果已经在测量，再次点击则停止
    if (m_isDynamicMeasuring)
    {
        m_isDynamicMeasuring = false;
        ui->bnDynamicMeasure->setText("开始动态测量"); // 可选：更新按钮文字
        ui->labellength->setText("0.00");
        ui->labelwidth->setText("0.00");
        QMessageBox::information(this, "提示", "已退出动态测量模式");
        return;
    }

    // 如果未开始，弹出提示并选择文件
    QMessageBox::information(this, "操作指引", "请选择已保存的转换矩阵文件 (.txt)");

    QString fileName = QFileDialog::getOpenFileName(
                this,
                tr("导入转换矩阵"),
                QString(), // 默认路径
                tr("Text Files (*.txt);;All Files (*)"));

    if (fileName.isEmpty())
    {
        return; // 用户取消
    }

    // 加载矩阵
    if (loadHomographyMatrix(fileName))
    {
        m_isDynamicMeasuring = true;
        ui->bnDynamicMeasure->setText("停止动态测量"); // 可选
        // [新增] 把矩阵传给子线程
        m_worker->setHomography(m_homographyMat);
        QMessageBox::information(this, "成功", "矩阵导入成功，开始实时测量！");
    }
    else
    {
        QMessageBox::warning(this, "错误", "矩阵文件格式错误或无法读取！");
    }
}

// ---------------------------------------------------------
// 3. 实现矩阵文件读取逻辑
// ---------------------------------------------------------
bool MainWindow::loadHomographyMatrix(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&file);
    std::vector<double> data;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith("#")) // 跳过空行和注释
            continue;

        // 按空格分割读取数字
        QStringList parts = line.split(QRegExp("\\s+"), QString::SkipEmptyParts);
        for (const QString &str : parts) {
            bool ok;
            double val = str.toDouble(&ok);
            if (ok) data.push_back(val);
        }
    }
    file.close();

    // 检查数据量，3x3矩阵应该有9个数据
    if (data.size() != 9)
        return false;

    // 存入 cv::Mat (3x3, double类型)
    m_homographyMat = cv::Mat(3, 3, CV_64F);
    int idx = 0;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            m_homographyMat.at<double>(i, j) = data[idx++];
        }
    }


    // =========================================================
    // [新增] 打印矩阵到终端
    // =========================================================
    qDebug() << "====== Homography Matrix Loaded ======";
    for (int i = 0; i < 3; i++) {
        QString rowLog;
        for (int j = 0; j < 3; j++) {
            // 保留6位小数，中间用 Tab 隔开
            double val = m_homographyMat.at<double>(i, j);
            rowLog += QString::number(val, 'f', 6) + "\t";
        }
        qDebug() << rowLog; // 打印当前行
    }
    qDebug() << "========================================";
    // =========================================================
    return true;
}

// ---------------------------------------------------------
// 4. 辅助函数：像素坐标 -> 物理坐标
// ---------------------------------------------------------
cv::Point2f MainWindow::pixelToWorld(const cv::Point2f &pixelPt)
{
    if (m_homographyMat.empty()) return cv::Point2f(0, 0);

    std::vector<cv::Point2f> src;
    src.push_back(pixelPt);
    std::vector<cv::Point2f> dst;

    // 使用透视变换
    cv::perspectiveTransform(src, dst, m_homographyMat);

    return dst[0];
}

// ---------------------------------------------------------
// 5. 实现动态测量核心逻辑 (图像处理)
// ---------------------------------------------------------
// -------------------------------------------------------------------------
// [优化版] processDynamicMeasure: 使用降采样加速，解决卡顿和延迟
// -------------------------------------------------------------------------
void MainWindow::processDynamicMeasure(cv::Mat &displayMat)
{
    // 1. 定义缩放因子 (0.2 表示缩小到原图的 1/5，速度提升约 25 倍)
    // 如果觉得精度不够，可以改成 0.25 或 0.5，但通常 0.2 足够找盒子了
    const double scale = 1;

    // 2. 图像预处理（在小图上进行）
    cv::Mat smallMat;
    cv::resize(displayMat, smallMat, cv::Size(), scale, scale);

    cv::Mat gray;
    if (smallMat.channels() == 3)
        cv::cvtColor(smallMat, gray, cv::COLOR_BGR2GRAY);
    else
        gray = smallMat.clone();

    // 高斯模糊 + 二值化 (参数可根据现场调整)
    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);
    cv::Mat bin;
    cv::threshold(gray, bin, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

    // 3. 查找轮廓 (在小图上找)
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    double maxArea = 0;
    std::vector<cv::Point> maxContour;

    // 4. 筛选最大轮廓 (注意：这里的面积是小图上的面积，阈值也要相应变小)
    // 原图阈值 1000，缩小 5 倍后面积缩小 25 倍，所以阈值大概设为 40 左右
    double minAreaThreshold = 1000 * scale * scale;

    for (const auto &cnt : contours)
    {
        double area = cv::contourArea(cnt);
        if (area > maxArea && area > minAreaThreshold)
        {
            maxArea = area;
            maxContour = cnt;
        }
    }

    // 5. 如果找到了物体
    if (!maxContour.empty())
    {
        // 获取最小外接矩形 (这是小图坐标)
        cv::RotatedRect rRectSmall = cv::minAreaRect(maxContour);
        cv::Point2f ptsSmall[4];
        rRectSmall.points(ptsSmall);

        // --- 关键：将坐标还原回原图尺寸 ---
        cv::Point2f ptsBig[4];
        for(int i=0; i<4; i++) {
            ptsBig[i] = ptsSmall[i] * (1.0 / scale);
        }

        // 6. 物理计算 (使用原图坐标 + 透视矩阵)
        cv::Point2f worldPts[4];
        for(int i=0; i<4; i++) {
            worldPts[i] = pixelToWorld(ptsBig[i]);
        }

        double sideA = cv::norm(worldPts[0] - worldPts[1]);
        double sideB = cv::norm(worldPts[1] - worldPts[2]);
        double length = std::max(sideA, sideB);
        double width  = std::min(sideA, sideB);

        // 7. 更新 UI
        ui->labellength->setText(QString::number(length, 'f', 2));
        ui->labelwidth->setText(QString::number(width, 'f', 2));

        // 8. 绘制结果 (在 displayMat 原图上画)
        for (int i = 0; i < 4; i++)
        {
            cv::line(displayMat, ptsBig[i], ptsBig[(i + 1) % 4], cv::Scalar(0, 255, 0), 2);
        }

        // 计算原图上的中心点用于显示文字
        cv::Point2f centerBig = rRectSmall.center * (1.0 / scale);
        std::string info = cv::format("L:%.1f W:%.1f", length, width);
        cv::putText(displayMat, info, centerBig, cv::FONT_HERSHEY_SIMPLEX,
                    1.0, cv::Scalar(0, 0, 255), 2);
    }
}
