library(scales,lib.loc="/u/rlavaee/R/packages")
library(plotrix,lib.loc="/u/rlavaee/R/packages")
library(ggplot2,lib.loc="/u/rlavaee/R/packages")
get_prog <- function(x) substring(x,0,nchar(x)-5)
args <- commandArgs(TRUE)
df_list <- list()
for(i in 1:length(args)){
	filename <- args[i]
	funcdata <- read.table(args[1],sep="\t",header=TRUE)
	df_list <- append(df_list,data.frame(prog=get_prog(filename),size=log2(funcdata$size),frequency=(funcdata$ncalls)))
}
big_df = do.call(rbind,df_list)
power2 <- function(x){ return(2^x) }
pdf(file=filename,width=4,height=4, family="CM",pointsize=9)
par(mar=c(3.8,3.5,1,1)+0.3)
ggplot(big_df,aes(x=size,colour=prog)+geom_density()+xlab("function size")+scale_x_continuous(labels=trans_format('identity',math_format(2^.x)))
dev.off()
embedCM(filename)
