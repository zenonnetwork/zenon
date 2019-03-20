#ifndef SPINNER_H
#define SPINNER_H

#include <QObject>
#include <QLabel>

#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>

/**
 * @brief The Spinner class
 * Implements a reusable spinner based on a series of static images.
 * The images are shown sequentially in a QPixmap in a QLabel, with an adjustable
 * delay between each two images in ms.
 * Images are expected to be named "<basename>-NNN.png", where NNN is [000...availableImages-1].
 * Other formats than png probably work too, but untested.
 */
class Spinner : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief SpinnerImage Construct a spinner for a predefined target.
     * This constructor initializes the given target with the first of the given images.
     * @param baseImage '<basename>' in '<basename>-NNN.png'
     * @param availableImages maximum in NNN -1.
     * @param parent
     */
    Spinner(QLabel* target, std::string baseImage, size_t availableImages, QSize const& spinnerSize, QObject *parent=nullptr);

    /**
     * @brief SpinnerImage Construct a spinner.
     * @param baseImage '<basename>' in '<basename>-NNN.png'
     * @param availableImages maximum in NNN -1.
     * @param parent
     */
    Spinner(std::string baseImage, size_t availableImages, QSize const& spinnerSize, QObject* parent=nullptr);

    ~Spinner();

public slots:
    /**
     * @brief start Start the spinner on a target given during construction.
     */
    void start();

    /**
     * @brief start Start the spinner with a latency of 50ms.
     * @param target Where to show the spinner.
     */
    void start(QLabel* target);

    /**
     * @brief start Start the spinner with a given latency
     * @param target Where to show the spinner
     * @param latencyMs delay between switching images
     */
    void start(QLabel* target, size_t latencyMs);

    /**
     * @brief stop the spinner thread.
     */
    void stop();

private:
    QLabel* initTarget{}; /* Optionally passed during construction to pre-set an image */
    std::string const baseImage; /* base image path */
    size_t const availableImages; /* Number of available images -1 */
    QSize const size; /* size of the spinner */

    // Preloaded, scaled pixmaps
    std::vector<QPixmap> images;

    std::thread spinnerThread; /* Updating the image constantly until stopped */
    bool stopped_{false}; /* Stops the thread */

    /**
     * @brief run Thread executor
     * @param target Where the spinner is shown
     */
    void run(QLabel* target, size_t latency);
};


#endif // SPINNER_H
