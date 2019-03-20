#include "spinner.h"
#include "spinner.moc"

Spinner::Spinner(QLabel* target, std::string baseImage, size_t availableImages, QSize const& spinnerSize, QObject* parent)
    : QObject{parent},
      initTarget{target},
      baseImage{baseImage},
      availableImages{availableImages},
      size{spinnerSize}
{
    images.resize(availableImages);
    for (size_t i{}; i < availableImages; ++i)
    {
        std::stringstream ss;
        ss << std::setw(3) << std::setfill('0') << i;
	auto const image{baseImage + ss.str()};
        QPixmap unsized{QString::fromStdString(image)};
        images[i] = unsized.scaled(spinnerSize.width(), spinnerSize.height(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    if (target)
    {
        target->setPixmap(images[0]);
    }
}

Spinner::Spinner(std::string baseImage, size_t availableImages, QSize const& spinnerSize, QObject* parent)
    : Spinner{nullptr, baseImage, availableImages, spinnerSize, parent}
{}

Spinner::~Spinner()
{
    stop(); // must be sure that the update thread is stopped
}

void Spinner::start()
{
    if (initTarget)
    {
        start(initTarget);
    }

}

void Spinner::start(QLabel* target)
{
    start(target, size_t{50});
}

void Spinner::start(QLabel* target, size_t latencyMs)
{
    if (spinnerThread.joinable())
    {
        return; // already started
    }

    stopped_ = false;
    spinnerThread = std::thread(&Spinner::run, this, target, latencyMs);
}

void Spinner::stop()
{
    stopped_ = true;
    if (spinnerThread.joinable())
    {
        spinnerThread.join();
    }
}

void Spinner::run(QLabel* target, size_t latency)
{
    auto const waitDuration{std::chrono::milliseconds{latency}};
    auto refTimePoint{std::chrono::system_clock::now() - waitDuration}; // last time the image was switched
    int currentImageId{}; // 0...availableImages-1
      
    // Thread 'main'
    while (!stopped_)
    {
        if (std::chrono::system_clock::now() - refTimePoint > waitDuration)
        {
            currentImageId = (currentImageId + 1) % availableImages;
	    auto& currentImage = images[currentImageId];
            target->setPixmap(currentImage);
            refTimePoint = std::chrono::system_clock::now();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

}
