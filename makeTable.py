import csv

def quote(value):
    return '"'+row[value]+'"'

def insertInput(inputField, inputPostfix, inputFrench, inputEnglish, inputType, inputMinSize, inputMaxSize, inputSize, inputMinValue, inputMaxValue):
    if inputType == "label":
        result = F'<label data-i18n-key="{inputField}">{inputField}{inputPostfix}</label>&nbsp;'
    else:
        result = F'<input type="{inputType}" id="{inputField}{inputPostfix}"'
        if inputMinSize:
            result += F' minlen="{inputMinSize}"'
        if inputMaxSize:
            result += F' maxlen="{inputMaxSize}"'
        if inputSize:
            result += F' minlen="{inputSize}"'
        result += ' onchange="changed(this);"/>'
    return result

htmStream = open ("tableau.htm", "wt", encoding='utf-8')
####cppStream = open ("tableau.cpp", "wt", encoding='utf-8')
with open('TablePanneau.txt', newline='') as csvfile:
    reader = csv.DictReader(csvfile,  delimiter='\t', quotechar='"')
    for row in reader:
        if row['Type']:
            htmStream.write(f"<tr>\n")
            htmStream.write(f'  <td align="right"><label data-i18n-key={quote("Field")}>{row["English"]}</label></td>\n')
            htmStream.write(f"  <td>{insertInput(row['Field'], 'Departure', row['French'], row['English'], row['Type'], row['MinSize'], row['MaxSize'], row['Size'], row['MinValue'], row['MaxValue'])}</td>\n")
            htmStream.write(f"  <td>{insertInput(row['Field'], 'Arrival', row['French'], row['English'], row['Type'], row['MinSize'], row['MaxSize'], row['Size'], row['MinValue'], row['MaxValue'])}</td>\n")
            htmStream.write(f"</tr>\n")
            ####cppStream.write(f'')
####cppStream.close()
htmStream.close()