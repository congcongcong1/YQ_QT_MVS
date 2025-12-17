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
#include <sstream>  // <--- 【新增这行】用于将 cv::Mat 转为字符串
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
    if (srcImg.isNull()) return;

    // 1. 预处理：统一转为 RGB888 或 Mono8
    QImage inputImg = srcImg;
    if (inputImg.format() != QImage::Format_Grayscale8 && inputImg.format() != QImage::Format_RGB888) {
        inputImg = inputImg.convertToFormat(QImage::Format_RGB888);
    }

    // 2. 转换为 Mat
    cv::Mat rawMat;
    if (inputImg.format() == QImage::Format_Grayscale8) {
        rawMat = cv::Mat(inputImg.height(), inputImg.width(), CV_8UC1, (void*)inputImg.constBits(), inputImg.bytesPerLine());
    } else {
        rawMat = cv::Mat(inputImg.height(), inputImg.width(), CV_8UC3, (void*)inputImg.constBits(), inputImg.bytesPerLine());
    }

    // 3. 离线去畸变 (如果已标定)
    cv::Mat displayMat;
    if (m_isCalibrated && !m_cameraMatrix.empty() && !m_distCoeffs.empty()) {
        cv::undistort(rawMat, displayMat, m_cameraMatrix, m_distCoeffs);
    } else {
        displayMat = rawMat.clone();
    }

    // 4. 更新 m_frameBuffer (供算法使用)
    // 注意：这里我们存原始 rawMat 还是存 displayMat？
    // 为了统一，建议存 displayMat (直图)，这样后面的算法都不用改
    {
        QMutexLocker locker(&m_frameMutex);
        size_t size = displayMat.total() * displayMat.elemSize();
        m_frameBuffer.resize(size);
        memcpy(m_frameBuffer.data(), displayMat.data, size);

        m_frameInfo.nWidth = displayMat.cols;
        m_frameInfo.nHeight = displayMat.rows;
        m_frameInfo.enPixelType = (displayMat.type() == CV_8UC1) ? PixelType_Gvsp_Mono8 : PixelType_Gvsp_RGB8_Packed; // 近似
        m_hasFrame = true;
    }

    // 5. 显示
    QImage qDisplay;
    if (displayMat.type() == CV_8UC1) {
        qDisplay = QImage(displayMat.data, displayMat.cols, displayMat.rows, displayMat.step, QImage::Format_Grayscale8).copy();
    } else {
        qDisplay = QImage(displayMat.data, displayMat.cols, displayMat.rows, displayMat.step, QImage::Format_RGB888).copy();
    }

    onNewFrame(qDisplay);
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
 * @brief 采集线程回调 (后台去畸变版)
 * @details
 * 1. 自动根据相机像素格式转换为 cv::Mat
 * 2. 如果已标定，直接在后台线程执行 cv::undistort (去畸变)，不占用 UI 线程资源
 * 3. 将处理好的“直图”转换为 QImage 发送给界面
 */
void MainWindow::ImageCallBackInner2(MV_FRAME_OUT* pstFrame, bool bAutoFree)
{
    // 1) 保存图片相关缓存（与 SaveImage 共享，保持不变）
    pthread_mutex_lock(&m_hSaveImageMux);
    if (NULL == m_pSaveImageBuf || pstFrame->stFrameInfo.nFrameLen > m_nSaveImageBufSize)
    {
        if (m_pSaveImageBuf) {
            free(m_pSaveImageBuf);
            m_pSaveImageBuf = NULL;
        }
        m_pSaveImageBuf = (unsigned char *)malloc(sizeof(unsigned char) * pstFrame->stFrameInfo.nFrameLen);
        if (m_pSaveImageBuf == NULL) {
            pthread_mutex_unlock(&m_hSaveImageMux);
            if (false == bAutoFree) m_pcMyCamera->FreeImageBuffer(pstFrame);
            return;
        }
        m_nSaveImageBufSize = pstFrame->stFrameInfo.nFrameLen;
    }
    memcpy(m_pSaveImageBuf, pstFrame->pBufAddr, pstFrame->stFrameInfo.nFrameLen);
    memcpy(&m_stImageInfo, &(pstFrame->stFrameInfo), sizeof(MV_FRAME_OUT_INFO_EX));
    pthread_mutex_unlock(&m_hSaveImageMux);

    // 2) 控制 UI 更新频率
    static int s_frameCount = 0;
    s_frameCount++;
    if (s_frameCount % 3 != 0) // 这里的数字可以调整，比如 % 3 表示每 3 帧显示 1 帧，降低 UI 压力
    {
        if (false == bAutoFree) m_pcMyCamera->FreeImageBuffer(pstFrame);
        return;
    }

    // 3) 图像处理核心区
    int width  = pstFrame->stFrameInfo.nWidth;
    int height = pstFrame->stFrameInfo.nHeight;
    MvGvspPixelType pixelType = pstFrame->stFrameInfo.enPixelType;

    // A. 将原始数据转换为 cv::Mat
    cv::Mat rawMat;
    if (pixelType == PixelType_Gvsp_Mono8) {
        // Mono8 直接引用数据
        rawMat = cv::Mat(height, width, CV_8UC1, pstFrame->pBufAddr);
    }
    else if (pixelType == PixelType_Gvsp_BGR8_Packed) {
        rawMat = cv::Mat(height, width, CV_8UC3, pstFrame->pBufAddr);
    }
    else if (pixelType == PixelType_Gvsp_RGB8_Packed) {
        cv::Mat src(height, width, CV_8UC3, pstFrame->pBufAddr);
        cv::cvtColor(src, rawMat, cv::COLOR_RGB2BGR); // 统一转为 BGR 方便 OpenCV 处理
    }

    // B. 缓存一份原始数据供某些算法使用（可选，保持你原有逻辑）
    {
        QMutexLocker locker(&m_frameMutex);
        // 注意：如果你后续算法都改用界面图了，这里其实可以简化
        if (!rawMat.empty()) {
            size_t size = rawMat.total() * rawMat.elemSize();
            m_frameBuffer.resize(size);
            memcpy(m_frameBuffer.data(), rawMat.data, size);
            m_frameInfo = pstFrame->stFrameInfo;
            m_hasFrame  = true;
        }
    }

    // C. 执行去畸变 (耗时操作放在这里！)
    cv::Mat finalMat;
    // 检查是否已标定且矩阵有效
    if (m_isCalibrated && !m_cameraMatrix.empty() && !m_distCoeffs.empty()) {
        // [关键] 在后台线程去畸变，不再卡死 UI
        cv::undistort(rawMat, finalMat, m_cameraMatrix, m_distCoeffs);
    } else {
        finalMat = rawMat; // 没标定就直接用原图
    }

    // D. 转为 QImage 发送
    QImage qimg;
    if (!finalMat.empty()) {
        if (finalMat.type() == CV_8UC1) {
            // 必须 deep copy，因为 finalMat 是局部变量
            qimg = QImage(finalMat.data, finalMat.cols, finalMat.rows, finalMat.step, QImage::Format_Grayscale8).copy();
        } else {
            // 彩色图转 RGB888
            cv::Mat rgbMat;
            cv::cvtColor(finalMat, rgbMat, cv::COLOR_BGR2RGB);
            qimg = QImage(rgbMat.data, rgbMat.cols, rgbMat.rows, rgbMat.step, QImage::Format_RGB888).copy();
        }
    }

    if (!qimg.isNull()) {
        emit sigNewFrame(qimg);
    }

    // 4) 释放资源
    if (false == bAutoFree) {
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
    // 1. 安全获取图像数据
    // ===================================================
    if (m_displayImage.isNull())
    {
        QMessageBox::information(this, tr("提示"), tr("当前没有图像"));
        return;
    }

    cv::Mat gray;

    // 【关键修复】：为了安全起见，先将图像转换为标准的 Grayscale8 格式
    // 这样保证了内存连续性，避免了 OpenCV 构造时因步长不匹配导致的崩溃
    QImage processImg = m_displayImage.convertToFormat(QImage::Format_Grayscale8);

    // 构造 cv::Mat 并深拷贝 (clone)，确保数据独立
    cv::Mat tempMat(processImg.height(), processImg.width(), CV_8UC1, (void*)processImg.constBits(), processImg.bytesPerLine());
    gray = tempMat.clone();

    if (gray.empty()) {
        QMessageBox::warning(this, tr("提示"), tr("图像转换失败"));
        return;
    }

    int imgW = gray.cols;
    int imgH = gray.rows;

    // ===================================================
    // 2. ROI 区域处理
    // ===================================================
    cv::Mat workGray = gray;
    cv::Point2f roiOffset(0.0f, 0.0f);

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
    // 3. 检测算法 (轮廓法)
    // ===================================================
    cv::GaussianBlur(workGray, workGray, cv::Size(9, 9), 2, 2);

    cv::Mat bin;
    cv::threshold(workGray, bin, 0, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    // ===================================================
    // 4. 结果提取
    // ===================================================
    struct CircleResult {
        cv::Point2f center;
        float radius;
        bool operator<(const CircleResult& other) const {
            if (std::abs(center.y - other.center.y) > 20) {
                return center.y < other.center.y;
            }
            return center.x < other.center.x;
        }
    };

    std::vector<CircleResult> results;

    for (const auto& cnt : contours)
    {
        double area = cv::contourArea(cnt);
        if (area < 200) continue;

        double perimeter = cv::arcLength(cnt, true);
        if (perimeter == 0) continue;
        double circularity = (4 * CV_PI * area) / (perimeter * perimeter);

        if (circularity > 0.8)
        {
            cv::Point2f center;
            float radius;
            cv::minEnclosingCircle(cnt, center, radius);

            float cx = center.x + roiOffset.x;
            float cy = center.y + roiOffset.y;
            results.push_back({{cx, cy}, radius});
        }
    }

    if (results.empty())
    {
        QMessageBox::information(this, tr("结果"), tr("未检测到任何圆形。"));
        m_hasCircleOverlay = false;
        m_circleCenterOverlay.clear();
        m_circleRadiusOverlay.clear();
        emit sigNewFrame(m_displayImage);
        return;
    }

    std::sort(results.begin(), results.end());

    // ===================================================
    // 5. 更新数据
    // ===================================================
    const int MAX_LABELS = 9;
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

    m_circleCenterOverlay.clear();
    m_circleRadiusOverlay.clear();

    int count = std::min((int)results.size(), MAX_LABELS);
    for (int i = 0; i < count; ++i)
    {
        const CircleResult& current = results[i];
        centerLabels[i]->setText(QString("(%1, %2)").arg(current.center.x, 0, 'f', 2).arg(current.center.y, 0, 'f', 2));
        radiusLabels[i]->setText(QString::number(current.radius, 'f', 2));

        m_circleCenterOverlay.append(current.center);
        m_circleRadiusOverlay.append(current.radius);
    }

    for (int i = count; i < MAX_LABELS; ++i) {
        centerLabels[i]->setText(tr("未知"));
        radiusLabels[i]->setText(tr("---"));
    }

    m_hasCircleOverlay = true;

    QMessageBox::information(this, tr("成功"), tr("检测到 %1 个圆形").arg(results.size()));

    // 触发重绘
    emit sigNewFrame(m_displayImage);
}

/**
 * @brief 重度防粘连版：找盒子四点
 * @details
 * 1. 使用大尺寸核 (13x13) 进行强力腐蚀，强制断开靠得很近的毛绒物体
 * 2. 找出所有矩形，UI 上显示“最大且最实心”的那个
 * 3. 修复了 QVector 和 std::vector 类型转换问题
 */
/**
 * @brief 找盒子四点 (适配去畸变版)
 * @details 直接使用 m_displayImage (去畸变图) 进行计算，确保显示的红框和物体完美贴合
 */
void MainWindow::on_bnFindBox_clicked()
{
    // =====================================================
    // 1. 直接使用当前显示的图像 (已去畸变)
    // =====================================================
    // 注意：这里不再需要锁 m_frameMutex，因为我们只读 UI 线程的 m_displayImage
    if (m_displayImage.isNull()) {
        QMessageBox::information(this, tr("提示"), tr("无图像"));
        return;
    }

    // 将 QImage 转为 cv::Mat (灰度)
    // 必须处理格式，防止 Format 不匹配导致的崩溃或花屏
    cv::Mat gray;
    QImage processImg = m_displayImage;

    // 如果不是 8位灰度，先转一下
    if (processImg.format() != QImage::Format_Grayscale8) {
        processImg = processImg.convertToFormat(QImage::Format_Grayscale8);
    }

    // 深拷贝数据到 Mat
    gray = cv::Mat(processImg.height(), processImg.width(), CV_8UC1, (void*)processImg.constBits(), processImg.bytesPerLine()).clone();

    if (gray.empty()) return;

    int imgW = gray.cols;
    int imgH = gray.rows;

    // =====================================================
    // 1.5 使用 ROI：如果有有效 ROI，则只在 ROI 范围内找
    // =====================================================
    cv::Mat workGray = gray;
    cv::Rect roiRect;
    cv::Point2f roiOffset(0.f, 0.f);

    if (m_hasRoi)
    {
        QRectF rQt = currentRoiRect();
        int x = std::max(0, (int)std::floor(rQt.left()));
        int y = std::max(0, (int)std::floor(rQt.top()));
        int w = std::min(imgW - x, (int)std::ceil(rQt.width()));
        int h = std::min(imgH - y, (int)std::ceil(rQt.height()));

        if (w > 10 && h > 10)
        {
            roiRect   = cv::Rect(x, y, w, h);
            workGray  = gray(roiRect).clone();
            roiOffset = cv::Point2f((float)x, (float)y);
        }
    }

    // =====================================================
    // 2. 图像处理算法 (保持原有的防粘连逻辑)
    // =====================================================
    cv::Mat blurred;
    cv::GaussianBlur(workGray, blurred, cv::Size(9, 9), 2, 2);

    // 智能二值化
    cv::Scalar meanVal = cv::mean(blurred);
    int threshType = cv::THRESH_BINARY | cv::THRESH_OTSU;
    // 如果背景很亮（白底黑物），则反转
    if (meanVal[0] > 100) {
        threshType = cv::THRESH_BINARY_INV | cv::THRESH_OTSU;
    }

    cv::Mat bin;
    cv::threshold(blurred, bin, 0, 255, threshType);

    // 重度防粘连
    cv::Mat kernelHeavy   = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(13, 13));
    cv::Mat kernelRestore = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(15, 15));
    cv::erode(bin, bin, kernelHeavy, cv::Point(-1,-1), 2);
    cv::dilate(bin, bin, kernelRestore, cv::Point(-1,-1), 2);

    // =====================================================
    // 3. 轮廓查找与筛选
    // =====================================================
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    std::vector<cv::Point2f> allRectPoints;
    std::vector<cv::Point2f> maxRectPoints;
    double maxScore = 0;

    for (const auto &cnt : contours)
    {
        double area = cv::contourArea(cnt);
        if (area < 1000) continue;

        cv::RotatedRect rRect = cv::minAreaRect(cnt);
        double rectArea = rRect.size.width * rRect.size.height;
        if (rectArea <= 1.0) continue;

        double solidity = area / rectArea;
        if (solidity < 0.6) continue;

        double score = area * solidity;

        // 多边形拟合
        std::vector<cv::Point> approx;
        cv::approxPolyDP(cnt, approx, 0.04 * cv::arcLength(cnt, true), true);

        std::vector<cv::Point2f> currentQuad;

        if (approx.size() == 4 && cv::isContourConvex(approx)) {
            for (auto p : approx) {
                currentQuad.push_back(cv::Point2f(p.x + roiOffset.x, p.y + roiOffset.y));
            }
        } else {
            cv::Point2f pts[4];
            rRect.points(pts);
            for (int i = 0; i < 4; i++) {
                currentQuad.push_back(cv::Point2f(pts[i].x + roiOffset.x, pts[i].y + roiOffset.y));
            }
        }

        // 亚像素优化 (在去畸变后的 gray 上做，这样坐标最准)
        bool inBound = true;
        for (const auto& p : currentQuad) {
            if (p.x < 5 || p.y < 5 || p.x > imgW - 5 || p.y > imgH - 5) inBound = false;
        }
        if (inBound) {
            cv::TermCriteria criteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 20, 0.01);
            cv::cornerSubPix(gray, currentQuad, cv::Size(9, 9), cv::Size(-1, -1), criteria);
        }

        sortRectPoints(currentQuad); // 排序 P1-P4

        allRectPoints.insert(allRectPoints.end(), currentQuad.begin(), currentQuad.end());

        if (score > maxScore) {
            maxScore = score;
            maxRectPoints = currentQuad;
        }
    }

    // =====================================================
    // 4. 更新 UI 和 叠加层
    // =====================================================
    if (!allRectPoints.empty())
    {
        // 更新 UI 标签
        if (maxRectPoints.size() == 4) {
            ui->labelBoxP1->setText(QString("(%1, %2)").arg(maxRectPoints[0].x, 0, 'f', 1).arg(maxRectPoints[0].y, 0, 'f', 1));
            ui->labelBoxP2->setText(QString("(%1, %2)").arg(maxRectPoints[1].x, 0, 'f', 1).arg(maxRectPoints[1].y, 0, 'f', 1));
            ui->labelBoxP3->setText(QString("(%1, %2)").arg(maxRectPoints[2].x, 0, 'f', 1).arg(maxRectPoints[2].y, 0, 'f', 1));
            ui->labelBoxP4->setText(QString("(%1, %2)").arg(maxRectPoints[3].x, 0, 'f', 1).arg(maxRectPoints[3].y, 0, 'f', 1));
        }

        // 更新叠加层数据
        m_lastQuad = maxRectPoints;
        m_hasQuad  = true;

        m_boxPointsOverlay.clear();
        for (const auto& p : allRectPoints) {
            m_boxPointsOverlay.push_back(p);
        }
        m_hasBoxOverlay = true;

        // 触发重绘 (会调用 onNewFrame，里面会画出这些框)
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
// ========================================================================
// 辅助函数：生成标定板的世界坐标 (Z=0)
// ========================================================================
void MainWindow::generateObjectPoints(std::vector<cv::Point3f>& objectPoints)
{
    objectPoints.clear();
    // 按照从上到下、从左到右的顺序生成 (0,0,0), (1,0,0), (2,0,0)...
    // 单位由 m_squareSize 决定 (如 mm)
    for (int i = 0; i < m_boardSize.height; ++i)
    {
        for (int j = 0; j < m_boardSize.width; ++j)
        {
            objectPoints.push_back(cv::Point3f(j * m_squareSize, i * m_squareSize, 0));
        }
    }
}

// ========================================================================
// 核心函数：点击按钮进行“单帧采集” -> “累计” -> “计算标定”
// ========================================================================

/**
 * @brief 畸变矫正/相机内参标定按钮 (高性能版)
 * @details 采用“降采样检测 + 原图亚像素提精”策略，解决 1200万像素卡死问题
 */
void MainWindow::on_bnRectify_clicked()
{
    // ========================================================================
    // 阶段一：模式选择 (仅在未开始采集时触发)
    // ========================================================================
    if (m_imagePointsSeq.empty())
    {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("相机内参标定"));
        msgBox.setText(tr("请选择操作模式：\n\n"
                          "1. 导入参数：直接加载之前保存的 XML/YML 文件。\n"
                          "2. 开始标定：拍摄棋盘格图片进行重新标定。"));

        QPushButton *btnImport = msgBox.addButton(tr("导入已有参数"), QMessageBox::ActionRole);
        QPushButton *btnStart  = msgBox.addButton(tr("开始新的标定"), QMessageBox::ActionRole);
        QPushButton *btnCancel = msgBox.addButton(tr("取消"), QMessageBox::RejectRole);

        msgBox.exec();

        if (msgBox.clickedButton() == btnCancel) return;

        // --- 选项 B: 导入已有参数 ---
        if (msgBox.clickedButton() == btnImport) {
            QString fileName = QFileDialog::getOpenFileName(
                this, tr("选择标定参数文件"), QString(), tr("标定文件 (*.xml *.yml);;所有文件 (*.*)")
            );
            if (fileName.isEmpty()) return;

            cv::FileStorage fs(fileName.toStdString(), cv::FileStorage::READ);
            if (fs.isOpened()) {
                fs["CameraMatrix"] >> m_cameraMatrix;
                fs["DistCoeffs"] >> m_distCoeffs;
                fs.release();
                m_isCalibrated = true;
                QMessageBox::information(this, tr("成功"), tr("相机内参已成功加载！"));
            } else {
                QMessageBox::critical(this, tr("错误"), tr("无法打开文件！"));
            }
            return;
        }
        // --- 选项 C: 开始新的标定 ---
        // 继续向下执行
    }

    // ========================================================================
    // 阶段二：安全获取图像
    // ========================================================================
    cv::Mat frame;
    bool hasFrame = false;

    // 1. 快速加锁拷贝数据
    {
        QMutexLocker locker(&m_frameMutex);
        if (m_hasFrame) {
            int w = m_frameInfo.nWidth;
            int h = m_frameInfo.nHeight;
            if (m_frameInfo.enPixelType == PixelType_Gvsp_Mono8) {
                frame = cv::Mat(h, w, CV_8UC1, m_frameBuffer.data()).clone();
            } else {
                cv::Mat temp(h, w, CV_8UC3, m_frameBuffer.data());
                cv::cvtColor(temp, frame, cv::COLOR_BGR2GRAY);
            }
            hasFrame = true;
        }
    }

    if (!hasFrame || frame.empty()) {
        QMessageBox::warning(this, tr("错误"), tr("当前没有图像帧，无法采集！"));
        return;
    }

    // ========================================================================
    // 阶段三：高性能角点检测 (Downscale -> Detect -> Upscale -> Refine)
    // ========================================================================

    QApplication::setOverrideCursor(Qt::WaitCursor); // 显示忙碌鼠标
    QApplication::processEvents();

    std::vector<cv::Point2f> corners;
    bool found = false;

    // --- 1. 降采样 (缩小到 1/4 大小，速度提升约 16 倍) ---
    // 1200万像素 -> 约 75万像素，检测极其迅速
    double scale = 0.25;
    cv::Mat smallFrame;
    cv::resize(frame, smallFrame, cv::Size(), scale, scale);

    // --- 2. 在小图上检测 ---
    // CALIB_CB_FAST_CHECK: 如果没棋盘格，快速返回
    int flags = cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE | cv::CALIB_CB_FAST_CHECK;
    std::vector<cv::Point2f> smallCorners;
    bool smallFound = cv::findChessboardCorners(smallFrame, m_boardSize, smallCorners, flags);

    if (smallFound)
    {
        // --- 3. 映射回原图坐标 ---
        for (const auto& pt : smallCorners) {
            corners.push_back(pt * (1.0 / scale)); // 坐标放大回去
        }

        // --- 4. 在原图上进行亚像素提精 (关键步骤，保证精度) ---
        // 只在角点周围小窗口计算，速度极快
        cv::cornerSubPix(frame, corners, cv::Size(11, 11), cv::Size(-1, -1),
                         cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.01));
        found = true;
    }

    QApplication::restoreOverrideCursor(); // 恢复鼠标

    if (found)
    {
        // 绘制预览图 (画在原图上)
        cv::Mat colorFrame;
        cv::cvtColor(frame, colorFrame, cv::COLOR_GRAY2BGR);
        cv::drawChessboardCorners(colorFrame, m_boardSize, corners, found);

        QImage qImg(colorFrame.data, colorFrame.cols, colorFrame.rows, colorFrame.step, QImage::Format_RGB888);
        emit sigNewFrame(qImg.copy()); // 刷新界面

        // 询问用户
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, tr("采集确认"),
                                      tr("检测成功！\n当前队列已有 %1 张。\n是否保留此帧？")
                                      .arg(m_imagePointsSeq.size()),
                                      QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::Yes)
        {
            m_imagePointsSeq.push_back(corners);
            std::vector<cv::Point3f> objectPoints;
            generateObjectPoints(objectPoints);
            m_objectPointsSeq.push_back(objectPoints);
        }
    }
    else
    {
        QMessageBox::warning(this, tr("未检测到"),
            tr("未检测到棋盘格！\n请确保标定板完全在视野内，且光照均匀。"));
        return;
    }

    // ========================================================================
    // 阶段四：计算标定 (仅当数据足够时)
    // ========================================================================
    if (m_imagePointsSeq.size() >= 15)
    {
        QMessageBox::StandardButton calcReply;
        calcReply = QMessageBox::question(this, tr("准备计算"),
                                      tr("已采集 %1 张图像，数据量足够。\n是否立即开始计算内参？\n(注意：计算过程可能会卡顿几秒钟)")
                                      .arg(m_imagePointsSeq.size()),
                                      QMessageBox::Yes | QMessageBox::No);

        if (calcReply == QMessageBox::Yes)
        {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            QApplication::processEvents();

            std::vector<cv::Mat> rvecs, tvecs;
            m_cameraMatrix = cv::Mat::eye(3, 3, CV_64F);
            m_distCoeffs = cv::Mat::zeros(8, 1, CV_64F);

            // 标定计算比较耗时，但这是最后一步，卡几秒是正常的
            double rms = cv::calibrateCamera(m_objectPointsSeq, m_imagePointsSeq, frame.size(),
                                             m_cameraMatrix, m_distCoeffs, rvecs, tvecs);

            m_isCalibrated = true;
            QApplication::restoreOverrideCursor();

            // 保存结果
            QString timeStr = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
            QString defaultName = QString("CameraCalibration_%1.xml").arg(timeStr);
            QString savePath = QFileDialog::getSaveFileName(this, tr("保存参数"), defaultName, tr("XML (*.xml);;YAML (*.yml)"));

            if (!savePath.isEmpty()) {
                cv::FileStorage fs(savePath.toStdString(), cv::FileStorage::WRITE);
                fs << "CameraMatrix" << m_cameraMatrix;
                fs << "DistCoeffs" << m_distCoeffs;
                fs << "ReprojectionError" << rms;
                fs.release();

                QMessageBox::information(this, tr("标定完成"),
                    tr("标定成功！RMS误差: %1\n参数已保存。").arg(rms));
            }

            // 重置队列
            m_imagePointsSeq.clear();
            m_objectPointsSeq.clear();
        }
    }
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
        // 修改为：使用 RANSAC 算法，允许 3.0 像素的重投影误差，剔除坏点
        m_H = findHomography(imagePoints, worldPoints, cv::RANSAC, 3.0);

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
/**
 * @brief 接收图像回调线程发来的新帧 QImage (完整修复版)
 * @details
 * 1. 强制格式转换防止崩溃
 * 2. 应用相机内参去畸变
 * 3. 发送给后台线程进行测量
 * 4. 绘制所有叠加层 (ROI, 盒子, 圆, 标定点, 测距尺)
 */
/**
 * @brief 接收图像回调线程发来的新帧 QImage (防崩溃修复版)
 * @details
 * 重点修复：在 cv::Mat 和 QImage 转换时显式处理 step (步长)，防止内存不对齐导致的崩溃
 */
/**
 * @brief UI 刷新函数 (纯净版)
 * @details
 * 1. 不再执行去畸变 (上游已经做好了)
 * 2. 专门负责绘制各种叠加层 (圆、框、字)
 * 3. 绝对不会因为重复去畸变而崩溃
 */
void MainWindow::onNewFrame(const QImage &img)
{
    if (img.isNull()) return;

    // 1. 保存当前显示的图像
    // 因为上游发来的已经是“直图”了，直接存下来即可
    m_displayImage = img.copy();

    QPixmap pix = QPixmap::fromImage(m_displayImage);

    // 2. 发送给后台测量线程 (如果开启了动态测量)
    if (m_isDynamicMeasuring && !m_homographyMat.empty())
    {
        // 同样，发给后台的也是直图，非常完美
        double scale = 1.0;
        cv::Mat smallMat;

        QImage processImg = m_displayImage;
        if (processImg.format() != QImage::Format_Grayscale8)
            processImg = processImg.convertToFormat(QImage::Format_Grayscale8);

        cv::Mat temp(processImg.height(), processImg.width(), CV_8UC1, (void*)processImg.constBits(), processImg.bytesPerLine());
        smallMat = temp.clone();

        emit startWorkerProcess(smallMat, scale);
    }

    // 3. 绘制各种叠加层 (Overlay)
    if (!pix.isNull())
    {
        QPainter painter(&pix);
        painter.setRenderHint(QPainter::Antialiasing, true);

        // --- A. 动态测量结果 ---
        if (m_isDynamicMeasuring && !m_lastMeasurePts.isEmpty()) {
            QPen pen(Qt::green); pen.setWidth(4); painter.setPen(pen); painter.setBrush(Qt::NoBrush);
            painter.drawPolygon(QPolygonF(m_lastMeasurePts));

            // 文字
            QPointF center(0, 0);
            for(const auto& pt : m_lastMeasurePts) center += pt;
            if(m_lastMeasurePts.size() > 0) center /= m_lastMeasurePts.size();

            QPen penText(Qt::red); painter.setPen(penText);
            QFont font = painter.font(); font.setPixelSize(60); font.setBold(true); painter.setFont(font);
            QString text = QString("L:%1 W:%2").arg(m_lastLen, 0, 'f', 1).arg(m_lastWid, 0, 'f', 1);
            QFontMetrics fm(font);
            painter.drawText(center.x() - fm.width(text)/2, center.y(), text);
        }

        // --- B. 圆检测结果 (红圈) ---
        if (m_hasCircleOverlay && !m_circleRadiusOverlay.isEmpty()) {
             QPen penCircle(Qt::red); penCircle.setWidth(3); painter.setPen(penCircle); painter.setBrush(Qt::NoBrush);
             for (int i = 0; i < m_circleRadiusOverlay.size(); ++i) {
                 if (i >= m_circleCenterOverlay.size()) break;
                 float r = m_circleRadiusOverlay[i];
                 if (r > 0.0f) {
                     cv::Point2f c = m_circleCenterOverlay[i];
                     painter.drawEllipse(QPointF(c.x, c.y), r, r);
                 }
             }
        }

        // --- C. 九点标定点 ---
        if (!m_calibPoints.isEmpty()) {
             QPen penCalib(Qt::green); penCalib.setWidth(3); painter.setPen(penCalib);
             for (int i=0; i<m_calibPoints.size(); ++i) {
                 QPointF pt(m_calibPoints[i].x, m_calibPoints[i].y);
                 painter.drawEllipse(pt, 5, 5);
                 painter.drawText(pt + QPointF(6, -6), QString::number(i + 1));
             }
        }

        // --- D. 找盒子结果 ---
        if (m_hasBoxOverlay && !m_boxPointsOverlay.isEmpty()) {
            QPen penAll(Qt::yellow); penAll.setWidth(1); painter.setPen(penAll); painter.setBrush(Qt::NoBrush);
            for (int i = 0; i < m_boxPointsOverlay.size(); i += 4) {
                if (i+3 >= m_boxPointsOverlay.size()) break;
                QPolygonF poly;
                poly << QPointF(m_boxPointsOverlay[i].x, m_boxPointsOverlay[i].y)
                     << QPointF(m_boxPointsOverlay[i+1].x, m_boxPointsOverlay[i+1].y)
                     << QPointF(m_boxPointsOverlay[i+2].x, m_boxPointsOverlay[i+2].y)
                     << QPointF(m_boxPointsOverlay[i+3].x, m_boxPointsOverlay[i+3].y);
                painter.drawPolygon(poly);
            }
        }
        if (m_hasQuad && m_lastQuad.size() == 4) {
            QPen penSel(Qt::red); penSel.setWidth(3); painter.setPen(penSel); painter.setBrush(Qt::NoBrush);
            QPolygonF poly;
            for(const auto& p : m_lastQuad) poly << QPointF(p.x, p.y);
            painter.drawPolygon(poly);
        }

        // --- E. ROI 和 测距 ---
        if (m_hasRoi) {
            QRectF r = currentRoiRect();
            if (r.width()>1 && r.height()>1) {
                QPen p(Qt::green); p.setWidth(2); p.setStyle(Qt::DashLine); painter.setPen(p);
                painter.drawRect(r);
            }
        }
        if (m_isMeasuring) {
            QPen p(Qt::cyan); p.setWidth(2); p.setStyle(Qt::DashLine); painter.setPen(p);
            painter.drawLine(m_measureStart.x, m_measureStart.y, m_measureEnd.x, m_measureEnd.y);
            float dist = std::sqrt(std::pow(m_measureStart.x-m_measureEnd.x,2) + std::pow(m_measureStart.y-m_measureEnd.y,2));
            painter.setPen(Qt::yellow);
            painter.drawText((m_measureStart.x+m_measureEnd.x)/2, (m_measureStart.y+m_measureEnd.y)/2, QString::number(dist,'f',1));
        }

        // FPS
        m_frameCountForFps++;
        if (m_fpsTimer.elapsed() >= 1000) {
            m_currentFps = m_frameCountForFps * 1000.0 / m_fpsTimer.elapsed();
            m_frameCountForFps = 0; m_fpsTimer.restart();
            ui->labelFps->setText(QString::number(m_currentFps, 'f', 1));
        }
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
