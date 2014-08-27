library(ggplot2)
library(reshape2)
stresses <- c(1,2,4,10)
windows <- c(2,4,6,8,10,12,14,20,25,30,35,40)
big_df = data.frame()
for(stress in stresses){
	data <- read.table(sprintf("stress%d.data",stress),row.names=1,skip=1,colClasses=c("character",rep("numeric",length(windows))))
	#colnames(data) = windows
	df = data.frame(t(data),stress=stress,window=windows)
	#print(df)
	big_df = rbind(big_df,df)
}

big_df.m <- melt(big_df , id=c('stress','window'),variable.name='input',value.name='Speedup')
print(big_df.m)


saveto <- "stress.pdf"
pdf(file=saveto, width=7.5, height=2.8, family="CM", pointsize=9)
p <- ggplot(big_df.m,aes(window,Speedup,group=stress,color=stress))
p <- p + facet_grid(. ~ input)
p <- p + geom_path()
p

dev.off()
embedCM(saveto)
