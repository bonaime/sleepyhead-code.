#include "report.h"
#include "ui_report.h"
#include <QMessageBox>
#include <QBuffer>
#include <Graphs/gYAxis.h>
#include <Graphs/gXAxis.h>
//#include <QTimer>
#include <QPrinter>
#include <QPrintDialog>
#include <QRegExp>
#include <QFile>

Report::Report(QWidget *parent, Profile * _profile, gGraphView * shared, Overview * overview) :
    QWidget(parent),
    ui(new Ui::Report),
    profile(_profile),
    m_overview(overview)
{
    ui->setupUi(this);

    Q_ASSERT(profile!=NULL);

    GraphView=new gGraphView(this,shared);
    setMaximumSize(graph_print_width,800);
    setMinimumSize(graph_print_width,800);
    GraphView->setMaximumSize(graph_print_width,graph_print_height);
    GraphView->setMinimumSize(graph_print_width,graph_print_height);

    GraphView->hide();

    // Reusing the layer data from overview screen,
    // (Can't reuse the graphs objects without breaking things)

    graphs["Usage"]=UC=new gGraph(GraphView,"Usage",graph_print_height,0);
    UC->AddLayer(m_overview->uc);

    graphs["AHI"]=AHI=new gGraph(GraphView,"AHI",graph_print_height,0);
    AHI->AddLayer(m_overview->bc);

    graphs["Pressure"]=PR=new gGraph(GraphView,"Pressure",graph_print_height,0);
    PR->AddLayer(m_overview->pr);

    graphs["Leaks"]=LK=new gGraph(GraphView,"Leaks",graph_print_height,0);
    LK->AddLayer(m_overview->lk);

    graphs["%PB"]=NPB=new gGraph(GraphView,"% in PB",graph_print_height,0);
    NPB->AddLayer(m_overview->npb);


    for (QHash<QString,gGraph *>::iterator g=graphs.begin();g!=graphs.end();g++) {
        gGraph *gr=g.value();
        gr->AddLayer(new gYAxis(),LayerLeft,gYAxis::Margin);
        gXAxis *gx=new gXAxis();
        gx->setUtcFix(true);
        gr->AddLayer(gx,LayerBottom,0,gXAxis::Margin);
        gr->AddLayer(new gXGrid());
    }

    GraphView->hideSplitter();
    m_ready=false;
}

Report::~Report()
{
    for (QHash<QString,gGraph *>::iterator g=graphs.begin();g!=graphs.end();g++) {
        delete g.value();
    }
    delete ui;
}
void Report::ReloadGraphs()
{

    for (QHash<QString,gGraph *>::iterator g=graphs.begin();g!=graphs.end();g++) {
        g.value()->setDay(NULL);
    }
    startDate=profile->FirstDay();
    endDate=profile->LastDay();
    for (QHash<QString,gGraph *>::iterator g=graphs.begin();g!=graphs.end();g++) {
        g.value()->ResetBounds();
    }
    m_ready=true;

}

QPixmap Report::Snapshot(gGraph * graph)
{
    QDateTime d1(startDate,QTime(0,0,0),Qt::UTC);
    qint64 first=qint64(d1.toTime_t())*1000L;
    QDateTime d2(endDate,QTime(23,59,59),Qt::UTC);
    qint64 last=qint64(d2.toTime_t())*1000L;

    GraphView->TrashGraphs();
    GraphView->AddGraph(graph);
    GraphView->ResetBounds();
    GraphView->SetXBounds(first,last);

    QPixmap pixmap=GraphView->renderPixmap(graph_print_width,graph_print_height,false);

    return pixmap;
}

QString Report::ParseTemplate(QString input)
{
    QString output;

    QRegExp rx("\\{\\{(.*)\\}\\}");
    rx.setMinimal(true);
    int lastpos=0,pos=0;

    while ((pos=rx.indexIn(input,pos))!=-1) {
        output+=input.mid(lastpos,pos-lastpos);
        QString block=rx.cap(1);

        QString code=block.section(".",0,0).toLower();
        QString key=block.section(".",1,-1);
        QHash<QString,QVariant> * pr=NULL;
        if (code=="profile") {
            pr=&profile->p_preferences;
        } else if (code=="pref") {
            pr=&pref.p_preferences;
        } else if (code=="local") {
            pr=&locals;
        }

        QString value;
        if (pr) {
            if (pr->contains(key)) {
                if ((*pr)[key].type()==QVariant::String){
                    value=(*pr)[key].toString();
                    value.replace("\n","<br/>");
                    output+=value;
                } else if ((*pr)[key].type()==QVariant::Double){
                    bool ok;
                    value=QString::number((*pr)[key].toDouble(&ok),'f',2);
                    if (ok) output+=value; else output+="[NaN]";
                } else if ((*pr)[key].type()==QVariant::Int){
                    bool ok;
                    value=QString::number((*pr)[key].toInt(&ok));
                    if (ok) output+=value; else output+="[NaN]";
                } else if ((*pr)[key].type()==QVariant::Date){
                    value=(*pr)[key].toDate().toString();
                    output+=value;
                } else {
                    qDebug() << "Unknown key type" << (*pr)[key].typeName() << " in " << code << "." << key  << "in template";
                }
            } else {
                qDebug() << "Key not found" << code << "." << key << "in template";
            }
        } else if (code=="graph") {
            if (graphs.contains(key)) {
                if (!graphs[key]->isEmpty()) {
                    QPixmap pixmap=Snapshot(graphs[key]);
                    QByteArray byteArray;
                    QBuffer buffer(&byteArray); // use buffer to store pixmap into byteArray
                    buffer.open(QIODevice::WriteOnly);
                    pixmap.save(&buffer, "PNG");
                //html += "<div align=center><img src=\"data:image/png;base64," + byteArray.toBase64() + "\" width=\""+QString::number(graph_print_width)+"px\" height=\""+QString::number(graph_print_height)+"px\"></div>\n"; //
                    output += "<div align=center><img src=\"data:image/png;base64," + byteArray.toBase64() + "\" width="+QString::number(graph_print_width)+"px height=\""+QString::number(graph_print_height)+"px\"></div>\n";
                }

            } else {
                qDebug() << "Graph not found" << key << "in template";
            }
        }
        pos+=rx.matchedLength();
        lastpos=pos;
    }
    output+=input.mid(lastpos); // will just return the input if no tags are used
    return output;

}

void Report::GenerateReport(QDate start, QDate end)
{
    if (!m_ready) return;
    startDate=start;
    endDate=end;

    locals["start"]=startDate;
    locals["end"]=endDate;
    locals["width"]=graph_print_width-10;

    if ((*profile).Exists("DOB") && !(*profile)["DOB"].toString().isEmpty()) {
        QDate dob=(*profile)["DOB"].toDate();
        QDateTime d1(dob,QTime(0,0,0));
        QDateTime d2(QDate::currentDate(),QTime(0,0,0));
        int years=d1.daysTo(d2)/365.25;
        locals["Age"]=years;
    }
    if (!(*profile).Exists("UnitSystem")) {
        (*profile)["UnitSystem"]="Metric";
    }
    if ((*profile).Exists("Height") && !(*profile)["Height"].toString().isEmpty()) {
        if ((*profile)["UnitSystem"].toString()=="Metric")
            locals["DistanceMeasure"]="cm";
        else locals["DistanceMeasure"]="inches";
    }
    QFile file(":/docs/template_overview.sht");
    file.open(QIODevice::ReadOnly);
    QString html=file.readAll();

    QString output=ParseTemplate(html);

    ui->webView->setHtml(output);
}

void Report::Print()
{
    QPrinter printer;
    //printer.setPrinterName("Print to File (PDF)");
    //printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setPrintRange(QPrinter::AllPages);
    printer.setOrientation(QPrinter::Portrait);
    //printer.setPaperSize(QPrinter::A4);
    printer.setResolution(QPrinter::HighResolution);
    //printer.setPageSize();
    printer.setFullPage(false);
    printer.setNumCopies(1);
    printer.setPageMargins(10,10,10,10,QPrinter::Millimeter);
    QPrintDialog *dialog = new QPrintDialog(&printer);
    //printer.setOutputFileName("printYou.pdf");
    if ( dialog->exec() == QDialog::Accepted) {
        ui->webView->print(&printer);
    }
}
