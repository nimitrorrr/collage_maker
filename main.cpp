#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QFileDialog>
#include <QMessageBox>
#include <QPixmap>
#include <QImage>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTimer>
#include <QProgressDialog>
#include <QThread>
#include <QGroupBox>
#include <QPainter>
#include <map>
#include <memory>

// Worker class for collage creation in separate thread
class CollageWorker : public QObject {
    Q_OBJECT
public:
    struct ImageData {
        QString path;
        QImage image;
    };

    CollageWorker(const std::map<std::pair<int,int>, ImageData>& data, 
                  int gridSize, int maxSize, const QString& outputPath)
        : imageData(data), gridSize(gridSize), maxCollageSize(maxSize), outputPath(outputPath) {}

signals:
    void finished(bool success, QString message);
    void progress(int value);

public slots:
    void process() {
        try {
            std::vector<QImage> images;
            for (int i = 0; i < gridSize; i++) {
                for (int j = 0; j < gridSize; j++) {
                    auto it = imageData.find({i, j});
                    if (it != imageData.end()) {
                        images.push_back(it->second.image);
                    } else {
                        images.push_back(QImage());
                    }
                }
            }

            emit progress(20);

            std::vector<QImage> processedImages;
            int minSize = INT_MAX;
            
            for (const auto& img : images) {
                if (!img.isNull()) {
                    QImage squared = cropCenterToSquare(img);
                    processedImages.push_back(squared);
                    minSize = std::min(minSize, squared.width());
                } else {
                    processedImages.push_back(QImage());
                }
            }

            if (minSize == INT_MAX) {
                emit finished(false, "Нет изображений для создания коллажа!");
                return;
            }

            emit progress(40);

            int collageSize = gridSize * minSize;
            if (collageSize > maxCollageSize) {
                minSize = maxCollageSize / gridSize;
                collageSize = gridSize * minSize;
            }

            emit progress(60);

            QImage collage(collageSize, collageSize, QImage::Format_RGB32);
            collage.fill(Qt::white);

            QPainter painter(&collage);
            
            for (size_t i = 0; i < processedImages.size(); i++) {
                if (!processedImages[i].isNull()) {
                    int row = static_cast<int>(i) / gridSize;
                    int col = static_cast<int>(i) % gridSize;
                    
                    QImage resized = processedImages[i].scaled(minSize, minSize, 
                                                               Qt::IgnoreAspectRatio, 
                                                               Qt::SmoothTransformation);
                    painter.drawImage(col * minSize, row * minSize, resized);
                }
                
                emit progress(60 + (static_cast<int>(i) * 35) / static_cast<int>(processedImages.size()));
            }

            emit progress(95);

            bool saved = collage.save(outputPath, "PNG", 100);
            
            emit progress(100);

            if (saved) {
                emit finished(true, QString("Коллаж %1x%1 сохранен как:\n%2")
                             .arg(collageSize).arg(outputPath));
            } else {
                emit finished(false, "Ошибка сохранения файла!");
            }

        } catch (const std::exception& e) {
            emit finished(false, QString("Ошибка: %1").arg(e.what()));
        }
    }

private:
    std::map<std::pair<int,int>, ImageData> imageData;
    int gridSize;
    int maxCollageSize;
    QString outputPath;

    QImage cropCenterToSquare(const QImage& img) {
        int width = img.width();
        int height = img.height();
        int newSize = std::min(width, height);
        int left = (width - newSize) / 2;
        int top = (height - newSize) / 2;
        return img.copy(left, top, newSize, newSize);
    }
};

// Custom label for drag & drop cells
class ImageCell : public QLabel {
    Q_OBJECT
public:
    ImageCell(int row, int col, QWidget* parent = nullptr) 
        : QLabel(parent), row(row), col(col), hasImage(false) {
        setAcceptDrops(true);
        setAlignment(Qt::AlignCenter);
        setStyleSheet("QLabel { background-color: lightgray; border: 1px solid gray; }");
        setFrameStyle(QFrame::Box);
        setText(QString("%1x%2").arg(row+1).arg(col+1));
        setMinimumSize(50, 50);
    }

    void setImageData(const QPixmap& pixmap, const QString& filename) {
        setPixmap(pixmap);
        hasImage = true;
        setStyleSheet("QLabel { background-color: lightgreen; border: 1px solid gray; }");
        setToolTip(filename);
    }

    void clearImage() {
        clear();
        hasImage = false;
        setStyleSheet("QLabel { background-color: lightgray; border: 1px solid gray; }");
        setText(QString("%1x%2").arg(row+1).arg(col+1));
        setToolTip("");
    }

    bool isEmpty() const { return !hasImage; }
    int getRow() const { return row; }
    int getCol() const { return col; }

signals:
    void imageDropped(int row, int col, QString filePath);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasUrls()) {
            event->acceptProposedAction();
            setStyleSheet("QLabel { background-color: lightyellow; border: 2px solid blue; }");
        }
    }

    void dragLeaveEvent(QDragLeaveEvent* event) override {
        if (hasImage) {
            setStyleSheet("QLabel { background-color: lightgreen; border: 1px solid gray; }");
        } else {
            setStyleSheet("QLabel { background-color: lightgray; border: 1px solid gray; }");
        }
    }

    void dropEvent(QDropEvent* event) override {
        const QMimeData* mimeData = event->mimeData();
        if (mimeData->hasUrls()) {
            QList<QUrl> urls = mimeData->urls();
            if (!urls.isEmpty()) {
                QString filePath = urls.first().toLocalFile();
                emit imageDropped(row, col, filePath);
            }
        }
        
        if (hasImage) {
            setStyleSheet("QLabel { background-color: lightgreen; border: 1px solid gray; }");
        } else {
            setStyleSheet("QLabel { background-color: lightgray; border: 1px solid gray; }");
        }
    }

private:
    int row, col;
    bool hasImage;
};

class CollageApp : public QMainWindow {
    Q_OBJECT
public:
    CollageApp(QWidget* parent = nullptr) : QMainWindow(parent), gridSize(3), maxCollageSize(4000) {
        setupUI();
        
        // Таймер для проверки размера окна
        checkSizeTimer = new QTimer(this);
        connect(checkSizeTimer, &QTimer::timeout, this, &CollageApp::checkWindowSize);
        checkSizeTimer->start(500); // Проверяем каждые 500ms
        
        QTimer::singleShot(100, this, &CollageApp::initializeGrid);
    }

private:
    int gridSize;
    int maxCollageSize;
    std::map<std::pair<int,int>, CollageWorker::ImageData> imageData;
    
    QWidget* centralWidget;
    QWidget* controlsContainer;
    QWidget* dropContainer;
    QGridLayout* gridLayout;
    QLabel* titleLabel;
    QLabel* infoLabel;
    QSpinBox* sizeSpinBox;
    QPushButton* clearButton;
    QPushButton* createButton;
    
    std::vector<std::vector<ImageCell*>> cells;
    
    QTimer* checkSizeTimer;
    int lastWidth;
    int lastHeight;

    void setupUI() {
        setWindowTitle("Продвинутый Коллаж - C++ Qt5");
        resize(800, 700);

        centralWidget = new QWidget(this);
        setCentralWidget(centralWidget);

        // Controls container
        controlsContainer = new QWidget(centralWidget);
        QVBoxLayout* controlsLayout = new QVBoxLayout(controlsContainer);

        // Title
        titleLabel = new QLabel("Создайте свой коллаж", controlsContainer);
        QFont titleFont = titleLabel->font();
        titleFont.setPointSize(14);
        titleFont.setBold(true);
        titleLabel->setFont(titleFont);
        titleLabel->setAlignment(Qt::AlignCenter);
        controlsLayout->addWidget(titleLabel);

        // Settings group
        QGroupBox* settingsGroup = new QGroupBox("Настройки", controlsContainer);
        QHBoxLayout* settingsLayout = new QHBoxLayout(settingsGroup);
        
        settingsLayout->addWidget(new QLabel("Размер сетки:"));
        sizeSpinBox = new QSpinBox();
        sizeSpinBox->setRange(1, 10);
        sizeSpinBox->setValue(gridSize);
        connect(sizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), 
                this, &CollageApp::onGridSizeChanged);
        settingsLayout->addWidget(sizeSpinBox);
        
        clearButton = new QPushButton("Очистить все");
        connect(clearButton, &QPushButton::clicked, this, &CollageApp::clearAll);
        settingsLayout->addWidget(clearButton);
        
        settingsLayout->addStretch();
        controlsLayout->addWidget(settingsGroup);

        // Info label
        infoLabel = new QLabel("Перетащите изображения в ячейки сетки", controlsContainer);
        infoLabel->setStyleSheet("QLabel { color: blue; }");
        infoLabel->setAlignment(Qt::AlignCenter);
        controlsLayout->addWidget(infoLabel);

        // Create button
        createButton = new QPushButton("Создать коллаж", controlsContainer);
        connect(createButton, &QPushButton::clicked, this, &CollageApp::createCollage);
        controlsLayout->addWidget(createButton);

        controlsLayout->addStretch();

        // Drop container with grid
        dropContainer = new QWidget(centralWidget);
        dropContainer->setStyleSheet("QWidget { background-color: #f0f0f0; border: 2px solid gray; }");
        gridLayout = new QGridLayout(dropContainer);
        gridLayout->setSpacing(2);
        gridLayout->setContentsMargins(10, 10, 10, 10);

        lastWidth = -1;
        lastHeight = -1;

        updateLayout();
    }

    void updateLayout() {
        // Очищаем текущее расположение
        if (centralWidget->layout()) {
            delete centralWidget->layout();
        }

        int windowWidth = width();
        int windowHeight = height();

        QBoxLayout* mainLayout;

        if (windowWidth > windowHeight * 1.1) {
            // Wide window: controls on right, grid on left
            mainLayout = new QHBoxLayout(centralWidget);
            mainLayout->addWidget(dropContainer, 1);
            mainLayout->addWidget(controlsContainer, 0);
        } else {
            // Tall/square window: controls on top, grid on bottom
            mainLayout = new QVBoxLayout(centralWidget);
            mainLayout->addWidget(controlsContainer, 0);
            mainLayout->addWidget(dropContainer, 1);
        }

        mainLayout->setContentsMargins(10, 10, 10, 10);
    }

    void initializeGrid() {
        lastWidth = width();
        lastHeight = height();
        updateLayout();
        recreateGrid();
    }
    
    void checkWindowSize() {
        int currentWidth = width();
        int currentHeight = height();
        
        // Проверяем изменился ли размер
        if (lastWidth != currentWidth || lastHeight != currentHeight) {
            lastWidth = currentWidth;
            lastHeight = currentHeight;
            
            // Обновляем layout и сетку
            updateLayout();
            recreateGrid();
        }
    }

    void recreateGrid() {
        // Clear existing grid
        while (gridLayout->count() > 0) {
            QLayoutItem* item = gridLayout->takeAt(0);
            if (item->widget()) {
                delete item->widget();
            }
            delete item;
        }
        cells.clear();

        // Calculate cell size - адаптивный по размеру окна
        int availableWidth = dropContainer->width() - 20;
        int availableHeight = dropContainer->height() - 20;
        
        if (availableWidth <= 1 || availableHeight <= 1) {
            availableWidth = 400;
            availableHeight = 400;
        }

        int cellSize = std::min(availableWidth / gridSize, availableHeight / gridSize);
        cellSize = std::max(cellSize, 50); // Minimum cell size

        // Create grid cells
        cells.resize(static_cast<size_t>(gridSize));
        for (int i = 0; i < gridSize; i++) {
            cells[static_cast<size_t>(i)].resize(static_cast<size_t>(gridSize));
            for (int j = 0; j < gridSize; j++) {
                ImageCell* cell = new ImageCell(i, j, dropContainer);
                cell->setFixedSize(cellSize, cellSize);
                cell->setScaledContents(true);
                
                connect(cell, &ImageCell::imageDropped, this, &CollageApp::onImageDropped);
                
                gridLayout->addWidget(cell, i, j);
                cells[static_cast<size_t>(i)][static_cast<size_t>(j)] = cell;
            }
        }

        // Restore images
        updateAllThumbnails(cellSize);
        updateInfoLabel();
    }

    void onImageDropped(int row, int col, QString filePath) {
        QStringList validExtensions = {"jpg", "jpeg", "png", "bmp", "gif", "tiff", "webp"};
        QFileInfo fileInfo(filePath);
        
        if (!validExtensions.contains(fileInfo.suffix().toLower())) {
            QMessageBox::warning(this, "Предупреждение", "Выбранный файл не является изображением");
            return;
        }

        QImage image(filePath);
        if (image.isNull()) {
            QMessageBox::critical(this, "Ошибка", "Не удалось загрузить изображение");
            return;
        }

        // Store image data
        imageData[{row, col}] = {filePath, image};

        // Create and display thumbnail
        int cellSize = cells[static_cast<size_t>(row)][static_cast<size_t>(col)]->width();
        QImage squared = cropCenterToSquare(image);
        QPixmap thumbnail = QPixmap::fromImage(squared.scaled(cellSize, cellSize, 
                                                              Qt::IgnoreAspectRatio, 
                                                              Qt::SmoothTransformation));
        
        cells[static_cast<size_t>(row)][static_cast<size_t>(col)]->setImageData(thumbnail, fileInfo.fileName());
        updateInfoLabel();
    }

    void updateAllThumbnails(int cellSize) {
        for (const auto& pair : imageData) {
            int row = pair.first.first;
            int col = pair.first.second;
            
            if (row < gridSize && col < gridSize) {
                QImage squared = cropCenterToSquare(pair.second.image);
                QPixmap thumbnail = QPixmap::fromImage(squared.scaled(cellSize, cellSize,
                                                                      Qt::IgnoreAspectRatio,
                                                                      Qt::SmoothTransformation));
                QFileInfo fileInfo(pair.second.path);
                cells[static_cast<size_t>(row)][static_cast<size_t>(col)]->setImageData(thumbnail, fileInfo.fileName());
            }
        }
    }

    QImage cropCenterToSquare(const QImage& img) {
        int width = img.width();
        int height = img.height();
        int newSize = std::min(width, height);
        int left = (width - newSize) / 2;
        int top = (height - newSize) / 2;
        return img.copy(left, top, newSize, newSize);
    }

    void updateInfoLabel() {
        int totalCells = gridSize * gridSize;
        int filledCells = static_cast<int>(imageData.size());
        infoLabel->setText(QString("Заполнено %1 из %2 ячеек").arg(filledCells).arg(totalCells));
        
        if (filledCells == totalCells) {
            infoLabel->setStyleSheet("QLabel { color: green; }");
        } else {
            infoLabel->setStyleSheet("QLabel { color: blue; }");
        }
    }

    void onGridSizeChanged(int newSize) {
        if (newSize != gridSize) {
            // Save current images
            auto oldData = imageData;
            
            // Update size
            gridSize = newSize;
            imageData.clear();
            
            // Recreate grid
            updateLayout();
            recreateGrid();
            
            // Restore images that fit in new grid
            for (const auto& pair : oldData) {
                int row = pair.first.first;
                int col = pair.first.second;
                if (row < newSize && col < newSize) {
                    onImageDropped(row, col, pair.second.path);
                }
            }
        }
    }

    void clearAll() {
        imageData.clear();
        updateLayout();
        recreateGrid();
    }

    void createCollage() {
        if (imageData.empty()) {
            QMessageBox::warning(this, "Предупреждение", "Добавьте хотя бы одно изображение!");
            return;
        }

        QString outputPath = QFileDialog::getSaveFileName(this, "Сохранить коллаж как", 
                                                          "", "PNG (*.png);;JPEG (*.jpg);;Все файлы (*.*)");
        if (outputPath.isEmpty()) {
            return;
        }

        // Create progress dialog
        QProgressDialog* progressDialog = new QProgressDialog("Создание коллажа...", "Отмена", 0, 100, this);
        progressDialog->setWindowModality(Qt::WindowModal);
        progressDialog->setMinimumDuration(0);
        progressDialog->setValue(0);

        // Create worker and thread
        QThread* thread = new QThread;
        CollageWorker* worker = new CollageWorker(imageData, gridSize, maxCollageSize, outputPath);
        worker->moveToThread(thread);

        connect(thread, &QThread::started, worker, &CollageWorker::process);
        connect(worker, &CollageWorker::progress, progressDialog, &QProgressDialog::setValue);
        connect(worker, &CollageWorker::finished, this, [=](bool success, QString message) {
            progressDialog->close();
            if (success) {
                QMessageBox::information(this, "Успех", message);
            } else {
                QMessageBox::critical(this, "Ошибка", message);
            }
            thread->quit();
        });
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        connect(progressDialog, &QProgressDialog::canceled, thread, &QThread::quit);

        thread->start();
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    CollageApp window;
    window.show();
    return app.exec();
}

#include "main.moc"
