import pandas as pd
import numpy as np

toDF = []

toDF2 = [["Tom", 20, "Business"], ["Jane", 21, "Data Analytics"], ["Alice", 19, "Business"]]
DF2 = pd.DataFrame(toDF2,columns = ["Name", "Age", "Major"])

DF2.head()