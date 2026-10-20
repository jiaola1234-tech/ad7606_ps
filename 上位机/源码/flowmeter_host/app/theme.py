APP_STYLE = """
QMainWindow {
    background: #f4f6f8;
}
QDockWidget {
    font-weight: 600;
    color: #243447;
}
QDockWidget::title {
    padding: 8px;
    background: #e9eef3;
    border-bottom: 1px solid #ccd6e0;
}
QWidget {
    font-family: "Microsoft YaHei", "Segoe UI", Arial, sans-serif;
    font-size: 13px;
}
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    min-height: 28px;
    border: 1px solid #c9d3dc;
    border-radius: 4px;
    padding: 2px 8px;
    background: white;
}
QPushButton {
    min-height: 30px;
    border: 1px solid #9fb0c0;
    border-radius: 4px;
    padding: 4px 10px;
    background: #ffffff;
}
QPushButton:hover {
    background: #eef5ff;
    border-color: #6c98c8;
}
QPushButton:pressed, QPushButton:checked {
    background: #d7e8fb;
}
QPushButton:disabled {
    color: #8b99a6;
    background: #edf0f3;
    border-color: #d6dde4;
}
QCheckBox {
    min-height: 24px;
}
QPlainTextEdit {
    border: 1px solid #c9d3dc;
    border-radius: 4px;
    background: #101820;
    color: #d7e2ee;
    font-family: Consolas, "Cascadia Mono", monospace;
    font-size: 12px;
}
QFrame#MatlabFrame {
    background: #ffffff;
    border: 1px solid #c9d3dc;
    border-radius: 6px;
}
QLabel#PanelTitle {
    color: #17212b;
    font-size: 19px;
    font-weight: 700;
}
QLabel#SectionLabel {
    margin-top: 8px;
    color: #506173;
    font-weight: 700;
}
QLabel#VelocityLabel {
    color: #17212b;
    font-size: 16px;
    font-weight: 700;
}
QStatusBar {
    background: #ffffff;
    border-top: 1px solid #d6dde4;
}
"""
