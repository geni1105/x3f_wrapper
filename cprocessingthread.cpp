#include "cprocessingthread.h"
#include <QSettings>
#include "settingsconstants.h"
#include <QProcess>


// see https://forum.qt.io/topic/60265/separate-qthread-to-update-qdialog-s-ui/3

CProcessingThread::CProcessingThread(const QStringList& inFiles,
                                     const QDir& inCurrentDir)
{
    settings = new QSettings;
    files = inFiles;
    currentDir = inCurrentDir;
    mRunning = true;
}

QStringList CProcessingThread::buildArgList(){
    QStringList output;
    if (!settings->value(SettingsConstants::denoise).toBool()){
        output << "-no-denoise";
    }
    if (settings->value(SettingsConstants::compress).toBool()){
        output << "-compress";
    }
    if (settings->value(SettingsConstants::ocl).toBool()){
        output << "-ocl";
    }
    // for the color, etc-- assume 0 is the default
    int val = settings->value(SettingsConstants::outputColor).toInt();
    if (val > 0){
        output << "-color";
        output << SettingsConstants::colorOptions[val];
    }
    val = settings->value(SettingsConstants::outputFormat).toInt();
    switch(val){
    case 1:  // "Embedded JPG"
        output << "-jpg";
        break;
    case 2:  // "TIFF"
        output << "-tiff";
        break;
    default:  // "DNG", ie, do nothing
        break;
    }
    val = settings->value(SettingsConstants::outputWB).toInt();
    if (val > 0){
        output << "-wb"; // We sure this is the right way to set up the WB call?
        output << SettingsConstants::wbOptions[val];
    }
    return output;
}

bool CProcessingThread::runX3FConversion(const QUrl& fileName, const QStringList& inArgs){
    int format = settings->value(SettingsConstants::outputFormat).toInt();
    QString endingString = ".dng";
    switch (format){
    case 1:
        endingString = ".jpg";
        break;
    case 2:
        endingString = ".tif";
        break;
    }

    if (QFile::exists(fileName.toLocalFile() + endingString)){
        return true;
    }
    QString tmpFileName = fileName.toLocalFile() + ".dng.tmp";
    if (QFile::exists(tmpFileName)){
        bool removalSuccessful = QFile::remove(tmpFileName);
        if (!removalSuccessful){
            emit error_message("A temp file exists and needs to be deleted.",
                               "A file named " + tmpFileName + " exists and needs to be deleted before processing can continue.");
            return false;
        }
    }

    QString converter = settings->value(SettingsConstants::x3fLocation).toString();

    QStringList arguments = inArgs;
    arguments << fileName.toLocalFile();
    int exitCode = QProcess::execute(converter, arguments);
    if (exitCode == -2){
        emit error_message("Is the x3f extractor properly set?",
                           "Error code: " + QString::number(exitCode) +
                           " was returned, suggesting that the x3f executable provided in the preferences is not runnable.  Argument list: [" + arguments.join(", ") + "]");

        return false;
    }
    if (exitCode != 0){
        emit error_message("Something went wrong.",
                           "Something happened while processing the image.  Error code: " + QString::number(exitCode) +
                           " argument list: [" + arguments.join(", ") + "]");

        return false;
    }
    return true;
}

bool CProcessingThread::runExifTool(QString exiftools, const QUrl& fileName, QString flatfield_correction){

    // availability of exiftool to be checked by caller

    QStringList exiftoolsargs;
    exiftoolsargs << "-overwrite_original";
    if( flatfield_correction != "")
        exiftoolsargs << flatfield_correction;
    exiftoolsargs << "-tagsFromFile";
    exiftoolsargs << fileName.toLocalFile();
    exiftoolsargs << "-all:all";

    int exitCode = 0;

    int format = settings->value(SettingsConstants::outputFormat).toInt();
    switch(format){
    case 1:
        //immaterial for jpgs
        break;
    case 2:
        exiftoolsargs << fileName.toLocalFile() + ".tif";
        exitCode = QProcess::execute(exiftools, exiftoolsargs);
        break;
    default:
        exiftoolsargs << fileName.toLocalFile() + ".dng";
        exitCode = QProcess::execute(exiftools, exiftoolsargs);
    }
    if (exitCode == -2){
        emit error_message("Is the exif extractor properly set?",
                           "Error code: " + QString::number(exitCode) +
                           " was returned, suggesting that the exif executable provided in the preferences is not runnable.  Argument list: [" + exiftoolsargs.join(", ") + "]");
        return false;
    }
    if (exitCode != 0){
        emit error_message("Something went wrong.",
                           "Something happened while transferring exif information.  Error code: " + QString::number(exitCode) +
                           " argument list: [" + exiftoolsargs.join(", ") + "]");
        return false;
    }
    return true;
}

bool CProcessingThread::runExifTool_GetFFParam(QString exiftools, const QUrl& fileName, QString& opcode_filename){

    // availability of exiftool to be checked by caller

    QStringList exiftoolsargs;
    exiftoolsargs << "-aperture";
    exiftoolsargs << "-model";
    exiftoolsargs << "-lensid";
    exiftoolsargs << fileName.toLocalFile();

    QProcess exiftool_process;

    exiftool_process.start(exiftools, exiftoolsargs);
    exiftool_process.waitForFinished(); // sets current thread to sleep and waits for exiftool_process end
    QString exiftool_output(exiftool_process.readAllStandardOutput());

    QString model_id;
    if (exiftool_output.contains("SIGMA DP1 Merrill"))
        model_id = "DP1M";
    else if (exiftool_output.contains("SIGMA DP2 Merrill"))
        model_id = "DP2M";
    else if (exiftool_output.contains("SIGMA DP3 Merrill"))
        model_id = "DP3M";
    else if (exiftool_output.contains("SIGMA SD1 Merrill"))
        model_id = "SD1M";
    else if (exiftool_output.contains("SIGMA SD1"))
        model_id = "SD1";
    else {
        emit error_message("Warning","No Merrill model ID EXIF data found!");
        return false;
    }

    QString aperture;
    int index;
    if (( index = exiftool_output.indexOf("Aperture                        : ")) < 0) {
        emit error_message("Warning","No aperture EXIF data found!");
        return false;
    }
    index += 34;
    while (exiftool_output.at(index) != '\n')
        aperture += exiftool_output.at(index++);

    QString lens_id;
    if (( index = exiftool_output.indexOf("Lens ID                         : ")) < 0) {
        emit error_message("Warning","No lens ID EXIF data found!");
        return false;
    }
    index += 34;
    while (exiftool_output.at(index) != '\n')
        lens_id += exiftool_output.at(index++);
    lens_id.replace(' ','_');
    lens_id.replace('|','_');  // for Sigma Art Lenses, there sometimes is an '|' in the name --> invalid in a file name on most OSs


    if ((model_id == "SD1M") || (model_id == "SD1"))
        opcode_filename = model_id + "_" + lens_id + "_FF_DNG_Opcodelist3_" + aperture;
    else
        opcode_filename = model_id + "_FF_DNG_Opcodelist3_" + aperture;

    return true;
}

void CProcessingThread::convertX3FFile(const QUrl& fileName, const QStringList& inArgs){
    if (runX3FConversion(fileName, inArgs) && mRunning){

        QString exiftools = settings->value(SettingsConstants::exifToolsLocation).toString();
        if (exiftools.length() < 1){
            return;  // no specified exif tool location, so don't run exiftools
        }
        QFileInfo info(exiftools);
        if (!info.isExecutable()){
            return;  // not an executable, so don't run exiftools
        }

        int format = settings->value(SettingsConstants::outputFormat).toInt();

        QString opcode_param = "";
        QString opcode_filename = "";
        if((format == 0) && (settings->value(SettingsConstants::flatfield).toBool())) {  // DNG output and flat-fielding
            if(runExifTool_GetFFParam(exiftools, fileName, opcode_filename)) {
                opcode_filename = settings->value(SettingsConstants::dngOpcodeLocation).toString() + "/" + opcode_filename;

                QFileInfo check_file(opcode_filename);
                    // check if opcode file exists and if yes: Is it really a file and not a directory?
                if( check_file.exists() && check_file.isFile() )
                    opcode_param = "-opcodelist3<=" + opcode_filename;
                else
                    emit error_message("Warning", "Flat-Field Correction Opcodes not found: \n" + opcode_filename);
            }
        }
        runExifTool(exiftools, fileName, opcode_param);
    }
}

void CProcessingThread::stopNow(){
    mRunning = false;
}

void CProcessingThread::run()
{
    QStringList arguments = buildArgList();
    try{
        for (int i = 0; i < files.size(); ++i) {
            if (mRunning){
                convertX3FFile(QUrl::fromLocalFile(currentDir.absoluteFilePath(files[i])),
                               arguments);
                emit progress(i, files.size());
            }
        }
    } catch(...){
        //something broke, but for now, do nothing.
    }
    if (mRunning){
        emit finishedProcessing();
    } else {
        emit canceledProcessing();
    }
    //sleep(1);  //to make sure the final signal is emitted before the thread is killed
}
