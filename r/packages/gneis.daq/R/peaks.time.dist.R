peaks.time.dist <- function(
  channel, master, srcDir, txtDir, pdfDir = NULL, htmlDir = NULL, 
  time.breaks, trigger = NA, 
  nevents = NA, filter = NULL, file.suffix = NULL, file.comment = NULL
) {

  my.make.src.filename.mask <- function() {
    res <- paste("peaks", "run[0-9]+", "txt", sep = ".")
    return (res)
  }

  my.process.file <- function(fn, accum) {
    my.df <- read.table(fn, header = T, sep = "")
    
    my.y <- my.df[[paste("CH", channel, "_PP_M", master, sep = "")]]
    my.trg <- my.df$TRG
    
    if (!is.null(filter)) {
      my.idx <- filter(my.df)
      if (!is.null(my.idx)) {
        my.y <- my.y[my.idx]
        my.trg <- my.trg[my.idx]
      }
    }
    
    if (!is.na(trigger)) {
      my.idx <- which(my.trg == trigger)
      my.y <- my.y[my.idx]
    }
    
    my.time.min <- time.breaks[1]
    my.time.max <- time.breaks[length(time.breaks)]
    my.y <- my.y[which(my.y >= my.time.min & my.y < my.time.max)]

    my.h.y <- hist(my.y, breaks = time.breaks, plot = F)

    if (is.null(accum)) {
      return(my.h.y$counts)
    } else {
      return(accum + my.h.y$counts)
    }
  }

  my.make.dest.filename <- function(dir, ext) {
    res <- paste(dir, "/peaks.ch", channel, sep = "")
    
    res <- paste(res, ".mst", master, sep = "")

    if (!is.na(trigger)) {
      res <- paste(res, ".trg", trigger, sep = "")
    }
    
    my.time.step <- round(time.breaks[2] - time.breaks[1])
    
    res <- paste(
      res, 
      paste("time", my.time.step, sep = ""), 
      sep = "."
    )
    
    if (!is.null(file.suffix)) {
      res <- paste(res, file.suffix, sep = ".")
    }
    
    res <- paste(res, "dist", ext, sep = ".")
    
    return (res)
  }
  
  my.make.result.filename <- function() {
    return(
      my.make.dest.filename(txtDir, "txt")
    )
  }

  my.make.plot.filename <- function(opt, suffix = NULL) {
    if (is.null(pdfDir)) {
      return()
    } else {
      return(
        my.make.dest.filename(pdfDir, "pdf")
      )
    }
  }
  
  my.make.html.filename <- function(opt, suffix = NULL) {
    if (is.null(htmlDir)) {
      return()
    } else {
      return(
        my.make.dest.filename(htmlDir, "html")
      )
    }
  }
  
  my.write.result <- function() {
    my.file.name <- my.make.result.filename()

    my.file.conn <- file(my.file.name)
    if (!is.null(file.comment)) {
      writeLines(paste("#", file.comment), my.file.conn)
    }
    close(my.file.conn)
    
    write.table(
      my.df,
      file = my.file.name,
      append = T,
      sep = "\t",
      row.names = F,
      col.names = T
    )
  }
  
  my.plot.title <- function(main) {
    my.res <- paste(main, paste("Channel #", channel, sep = ""), sep = ", ")
    if (!is.na(master)) {
      my.res <- paste(my.res, paste("Master #", master, sep = ""), sep = ", ")
    }
    if (!is.na(trigger)) {
      my.res <- paste(my.res, paste("Trigger #", trigger, sep = ""), sep = ", ")
    }
    if (!is.null(file.comment)) {
      my.res <- paste(my.res, file.comment, sep = ", ")
    }
    return(my.res)
  }
  
  my.plot <- function(my.accum) {
    library(ggplot2)
    pdf(my.make.plot.filename())
    
    my.p <- ggplot(
      data = my.df, 
      aes(x = TIME, y = COUNT)
    ) + 
      geom_line() +
      ggtitle(
        label = my.plot.title("Time Distribution")
      ) +
      scale_x_continuous(name = "Time, channels") + 
      scale_y_continuous(name = "Count") +
      theme_light()
    
    plot(my.p)
    
    if (!is.null(my.make.html.filename())) {
      library(plotly)
      library(tidyverse)
      library(htmlwidgets)
      htmlwidgets::saveWidget(ggplotly(my.p), my.make.html.filename())
    }
  }
  
  ######################################################
  # Entry point
  ######################################################
  
  my.accum <- NULL
  my.files <- list.files(path = srcDir, pattern = my.make.src.filename.mask(), full.names = T)
  for (my.fn in my.files) {
    my.accum <- my.process.file(my.fn, my.accum)
    if (!is.na(nevents) && sum(my.accum) >= nevents) {
      break
    }
  }

  my.df <- data.frame(
    TIME = tail(time.breaks, n = -1),
    COUNT = my.accum
  )
  
  my.write.result()
  if (!is.null(my.make.plot.filename())) {
    my.plot()
  }
  
}
